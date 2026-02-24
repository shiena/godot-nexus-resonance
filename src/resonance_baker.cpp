#include "resonance_baker.h"
#include "resonance_constants.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/resource_uid.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <atomic>
#include <cmath>

using namespace godot;

// Thread-safe counter for fallback probe data path when no path is set
static std::atomic<int> s_fallback_counter{0};

/// Convolution=0 -> BAKECONVOLUTION only; Parametric=1 -> BAKEPARAMETRIC only; Hybrid=2 or -1 -> both
static IPLReflectionsBakeFlags _bake_flags_from_reflection_type(int reflection_type) {
    if (reflection_type == 0) return static_cast<IPLReflectionsBakeFlags>(IPL_REFLECTIONSBAKEFLAGS_BAKECONVOLUTION);
    if (reflection_type == 1) return static_cast<IPLReflectionsBakeFlags>(IPL_REFLECTIONSBAKEFLAGS_BAKEPARAMETRIC);
    return static_cast<IPLReflectionsBakeFlags>(IPL_REFLECTIONSBAKEFLAGS_BAKECONVOLUTION | IPL_REFLECTIONSBAKEFLAGS_BAKEPARAMETRIC);
}

static void _fill_reflections_bake_params(IPLReflectionsBakeParams& out,
    IPLScene scene, IPLProbeBatch probe_batch, IPLSceneType scene_type,
    IPLOpenCLDevice opencl_device, IPLRadeonRaysDevice radeon_rays_device,
    IPLBakedDataVariation variation, int num_rays, int num_bounces, int reflection_type, int num_threads,
    const Vector3* endpoint_position = nullptr, float influence_radius = 0.0f) {
    out.scene = scene;
    out.probeBatch = probe_batch;
    out.sceneType = scene_type;
    out.openCLDevice = opencl_device;
    out.radeonRaysDevice = radeon_rays_device;
    out.identifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
    out.identifier.variation = variation;
    if (endpoint_position && influence_radius > 0.0f) {
        out.identifier.endpointInfluence.center = ResonanceUtils::to_ipl_vector3(*endpoint_position);
        out.identifier.endpointInfluence.radius = influence_radius;
    }
    out.bakeFlags = _bake_flags_from_reflection_type(reflection_type);
    out.numRays = num_rays;
    out.numBounces = num_bounces;
    out.numDiffuseSamples = 32;
    out.order = 1;
    out.simulatedDuration = resonance::kBakerSimulatedDuration;
    out.savedDuration = resonance::kBakerSimulatedDuration;
    out.numThreads = (num_threads < 1) ? 1 : num_threads;
    out.irradianceMinDistance = resonance::kBakerIrradianceMinDistance;
}

/// Load probe batch from ResonanceProbeData. Caller must call iplProbeBatchRelease.
static IPLProbeBatch _load_probe_batch_from_resource(IPLContext context, Ref<ResonanceProbeData> probe_data_res) {
    if (probe_data_res.is_null() || probe_data_res->get_data().is_empty()) return nullptr;
    PackedByteArray pba = probe_data_res->get_data();
    IPLSerializedObjectSettings sSettings{};
    sSettings.data = reinterpret_cast<IPLbyte*>(pba.ptrw());
    sSettings.size = pba.size();
    IPLSerializedObject sObj = nullptr;
    if (iplSerializedObjectCreate(context, &sSettings, &sObj) != IPL_STATUS_SUCCESS) return nullptr;
    IPLProbeBatch batch = nullptr;
    if (iplProbeBatchLoad(context, sObj, &batch) != IPL_STATUS_SUCCESS) {
        iplSerializedObjectRelease(&sObj);
        return nullptr;
    }
    iplSerializedObjectRelease(&sObj);
    iplProbeBatchCommit(batch);
    return batch;
}

static String _build_tres_content(const PackedByteArray& pba, Ref<ResonanceProbeData> probe_data_res, int reflection_type) {
    int64_t bph = probe_data_res->get_bake_params_hash();
    int64_t pph = probe_data_res->get_pathing_params_hash();
    int64_t ssp = probe_data_res->get_static_source_params_hash();
    int64_t slp = probe_data_res->get_static_listener_params_hash();
    String data_str = UtilityFunctions::var_to_str(pba);
    return "[gd_resource type=\"ResonanceProbeData\" format=3]\n\n[resource]\ndata = " + data_str +
        "\nbake_params_hash = " + String::num_int64(bph) +
        "\nbaked_reflection_type = " + String::num_int64(reflection_type) +
        "\npathing_params_hash = " + String::num_int64(pph) +
        "\nstatic_source_params_hash = " + String::num_int64(ssp) +
        "\nstatic_listener_params_hash = " + String::num_int64(slp) + "\n";
}

/// Save probe_data to disk. path must be non-empty. Returns true on success.
static bool _save_probe_data_to_disk(Ref<ResonanceProbeData> probe_data_res, const String& path,
    const PackedByteArray& pba, int reflection_type, IPLsize size, bool pathing_scheduled) {
    if (path.is_empty() || pathing_scheduled) return true;
    Error err = ResourceSaver::get_singleton()->save(probe_data_res, path, ResourceSaver::FLAG_CHANGE_PATH);
    if (err == OK) return true;
    String tres_path = path.get_basename() + ".tres";
    Ref<FileAccess> f = FileAccess::open(tres_path, FileAccess::WRITE);
    if (!f.is_valid()) {
        UtilityFunctions::push_error("BAKER ERROR: Could not save file. ResourceSaver failed (", (int)err, ") and fallback open failed.");
        return false;
    }
    String content = _build_tres_content(pba, probe_data_res, reflection_type);
    f->store_string(content);
    f->close();
    probe_data_res->take_over_path(tres_path);
    return true;
}

struct AdapterData {
    void (*cb)(float, void*);
    void* ud;
};

ResonanceBaker::ResonanceBaker() {}
ResonanceBaker::~ResonanceBaker() {}

PackedVector3Array ResonanceBaker::generate_manual_grid(const Transform3D& volume_transform, Vector3 extents, float spacing,
    int generation_type, float height_above_floor) {
    PackedVector3Array points;

    if (spacing <= 0.1f) spacing = 0.1f;
    Vector3 size = extents * 2.0f;

    if (generation_type == GEN_CENTROID) {
        // Single probe at volume center (Steam Audio IPL_PROBEGENERATIONTYPE_CENTROID)
        Vector3 local_center(0, 0, 0);
        Vector3 world_pos = volume_transform.xform(local_center);
        points.push_back(world_pos);
        return points;
    }

    if (generation_type == GEN_UNIFORM_FLOOR) {
        // Probes on horizontal plane at bottom of volume + height_above_floor (Steam Audio IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR)
        // Plane at local y = -extents.y + height_above_floor
        float plane_y = -extents.y + height_above_floor;
        int count_x = static_cast<int>(std::floor(size.x / spacing));
        int count_z = static_cast<int>(std::floor(size.z / spacing));
        if (count_x <= 0) count_x = 1;
        if (count_z <= 0) count_z = 1;

        float offset_x = (size.x < spacing) ? extents.x : spacing * 0.5f;
        float offset_z = (size.z < spacing) ? extents.z : spacing * 0.5f;

        for (int ix = 0; ix < count_x; ix++) {
            for (int iz = 0; iz < count_z; iz++) {
                Vector3 local_pos(-extents.x + (ix * spacing) + offset_x, plane_y, -extents.z + (iz * spacing) + offset_z);
                points.push_back(volume_transform.xform(local_pos));
            }
        }
        return points;
    }

    // GEN_VOLUME (default): Uniform 3D grid filling the volume
    int count_x = static_cast<int>(std::floor(size.x / spacing));
    int count_y = static_cast<int>(std::floor(size.y / spacing));
    int count_z = static_cast<int>(std::floor(size.z / spacing));
    if (count_x <= 0) count_x = 1;
    if (count_y <= 0) count_y = 1;
    if (count_z <= 0) count_z = 1;

    Vector3 local_start = -extents;
    Vector3 offset(spacing * 0.5f, spacing * 0.5f, spacing * 0.5f);
    if (size.x < spacing) offset.x = extents.x;
    if (size.y < spacing) offset.y = extents.y;
    if (size.z < spacing) offset.z = extents.z;

    for (int ix = 0; ix < count_x; ix++) {
        for (int iy = 0; iy < count_y; iy++) {
            for (int iz = 0; iz < count_z; iz++) {
                Vector3 local_pos = local_start + Vector3(
                    (ix * spacing) + offset.x,
                    (iy * spacing) + offset.y,
                    (iz * spacing) + offset.z
                );
                points.push_back(volume_transform.xform(local_pos));
            }
        }
    }
    return points;
}

static void IPLCALL _ipl_progress_adapter(IPLfloat32 progress, void* userData) {
    AdapterData* ad = static_cast<AdapterData*>(userData);
    if (ad && ad->cb) ad->cb(static_cast<float>(progress), ad->ud);
}

bool ResonanceBaker::bake_with_probe_array(IPLContext context, IPLScene scene, IPLSceneType scene_type,
    IPLOpenCLDevice opencl_device, IPLRadeonRaysDevice radeon_rays_device,
    const Transform3D& volume_transform, Vector3 extents, float spacing,
    int generation_type, float height_above_floor,
    int num_bounces, int num_rays, int reflection_type,
    Ref<ResonanceProbeData> probe_data_res,
    void (*progress_callback)(float, void*), void* progress_user_data, bool pathing_scheduled, int num_threads) {
    if (generation_type != GEN_CENTROID && generation_type != GEN_UNIFORM_FLOOR) {
        UtilityFunctions::push_error("BAKER: bake_with_probe_array only supports Centroid (0) and UniformFloor (1). Use bake_manual_grid for Volume.");
        return false;
    }
    if (probe_data_res.is_null() || !context || !scene) {
        UtilityFunctions::push_error("BAKER ERROR: bake_with_probe_array requires valid context, scene, and probe_data.");
        return false;
    }
    if (Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("BAKER: Using Steam Audio Probe Array API (scene-aware placement)...");
    }
    IPLProbeArray probeArray = nullptr;
    if (iplProbeArrayCreate(context, &probeArray) != IPL_STATUS_SUCCESS) {
        UtilityFunctions::push_error("BAKER ERROR: iplProbeArrayCreate failed.");
        return false;
    }
    IPLProbeGenerationParams genParams{};
    genParams.type = (generation_type == GEN_CENTROID) ? IPL_PROBEGENERATIONTYPE_CENTROID : IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR;
    genParams.spacing = spacing;
    genParams.height = height_above_floor;
    genParams.transform = ResonanceUtils::create_volume_transform_rotated(volume_transform, extents);
    iplProbeArrayGenerateProbes(probeArray, scene, &genParams);
    int num_probes = iplProbeArrayGetNumProbes(probeArray);
    if (num_probes == 0) {
        iplProbeArrayRelease(&probeArray);
        // Fallback: Steam Audio scene-aware UNIFORMFLOOR can return 0 (no floor detected).
        // Use our manual grid placement which places probes on a horizontal plane in the volume.
        if (generation_type == GEN_UNIFORM_FLOOR) {
            if (Engine::get_singleton()->is_editor_hint()) {
                UtilityFunctions::print("BAKER: Steam Audio UniformFloor returned 0 probes (scene may have no detectable floor). Using flat-plane fallback—probes placed on horizontal plane in volume, NOT on ResonanceGeometry floor. Consider GEN_VOLUME for full 3D coverage.");
            }
            PackedVector3Array points = generate_manual_grid(volume_transform, extents, spacing, generation_type, height_above_floor);
            if (points.size() > 0) {
                return bake_manual_grid(context, scene, scene_type, opencl_device, radeon_rays_device,
                    points, num_bounces, num_rays, reflection_type, probe_data_res, progress_callback, progress_user_data, pathing_scheduled, num_threads);
            }
        }
        UtilityFunctions::push_error("BAKER ERROR: Steam probe array generated 0 probes. Check volume and scene geometry.");
        return false;
    }
    if (Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("BAKER: Probe array generated ", num_probes, " probes.");
    }
    PackedVector3Array positions_for_viz;
    for (int i = 0; i < num_probes; i++) {
        IPLSphere sphere = iplProbeArrayGetProbe(probeArray, i);
        positions_for_viz.push_back(ResonanceUtils::to_godot_vector3(sphere.center));
    }
    IPLProbeBatch probeBatch = nullptr;
    iplProbeBatchCreate(context, &probeBatch);
    iplProbeBatchAddProbeArray(probeBatch, probeArray);
    iplProbeArrayRelease(&probeArray);
    iplProbeBatchCommit(probeBatch);
    IPLReflectionsBakeParams bakeParams{};
    _fill_reflections_bake_params(bakeParams, scene, probeBatch, scene_type, opencl_device, radeon_rays_device,
        IPL_BAKEDDATAVARIATION_REVERB, num_rays, num_bounces, reflection_type, num_threads);
    AdapterData adapter = { progress_callback, progress_user_data };
    iplReflectionsBakerBake(context, &bakeParams,
        (progress_callback && progress_user_data) ? _ipl_progress_adapter : nullptr,
        (progress_callback && progress_user_data) ? &adapter : nullptr);
    IPLSerializedObjectSettings serialSettings{};
    IPLSerializedObject serializedObject = nullptr;
    iplSerializedObjectCreate(context, &serialSettings, &serializedObject);
    iplProbeBatchSave(probeBatch, serializedObject);
    iplProbeBatchRelease(&probeBatch);
    IPLsize size = iplSerializedObjectGetSize(serializedObject);
    if (size == 0) {
        UtilityFunctions::push_error("Nexus Resonance Bake: iplReflectionsBakerBake produced no data.");
        iplSerializedObjectRelease(&serializedObject);
        return false;
    }
    IPLbyte* data = iplSerializedObjectGetData(serializedObject);
    PackedByteArray pba;
    pba.resize((int64_t)size);
    memcpy(pba.ptrw(), data, size);
    probe_data_res->set_data(pba);
    probe_data_res->set_probe_positions(positions_for_viz);
    probe_data_res->set_baked_reflection_type(reflection_type);
    iplSerializedObjectRelease(&serializedObject);
    String path = probe_data_res->get_path();
    if (!path.is_empty() && path.begins_with("uid://")) {
        path = ResourceUID::get_singleton()->uid_to_path(path);
    }
    if (path.is_empty()) {
        int n = s_fallback_counter.fetch_add(1) + 1;
        path = "res://audio_data/baked_probe_data_" + String::num_int64(n) + ".tres";
        Ref<DirAccess> da = DirAccess::open("res://");
        if (da.is_valid() && !da->dir_exists("audio_data")) {
            da->make_dir("audio_data");
        }
    }
    if (!_save_probe_data_to_disk(probe_data_res, path, pba, reflection_type, size, pathing_scheduled)) {
        return false;
    }
    if (Engine::get_singleton()->is_editor_hint() && !path.is_empty()) {
        int kb = (int)((size + 512) / 1024);
        UtilityFunctions::print("BAKER: Saved ", kb, " kilobytes (Probe Array bake).");
    }
    return true;
}

bool ResonanceBaker::bake_manual_grid(IPLContext context, IPLScene scene, IPLSceneType scene_type, IPLOpenCLDevice opencl_device, IPLRadeonRaysDevice radeon_rays_device, const PackedVector3Array& probe_positions, int num_bounces, int num_rays, int reflection_type, Ref<ResonanceProbeData> probe_data_res, void (*progress_callback)(float, void*), void* progress_user_data, bool pathing_scheduled, int num_threads) {
    if (Engine::get_singleton()->is_editor_hint()) {
        const char* refl_name = (reflection_type == 0) ? "Convolution" : (reflection_type == 1) ? "Parametric" : "Hybrid";
        String msg = pathing_scheduled
            ? String("BAKER: Starting Bake (") + refl_name + " + Pathing) Process..."
            : String("BAKER: Starting Bake (") + refl_name + ") Process...";
        UtilityFunctions::print(msg);
    }

    if (probe_positions.size() == 0) {
        UtilityFunctions::push_error("BAKER ERROR: No points to bake!");
        return false;
    }
    if (probe_data_res.is_null()) {
        UtilityFunctions::push_error("BAKER ERROR: Resource is null.");
        return false;
    }
    if (!context || !scene) {
        UtilityFunctions::push_error("BAKER ERROR: Steam Audio Context/Scene missing.");
        return false;
    }

    // 1. Create Batch
    IPLProbeBatch probeBatch = nullptr;
    iplProbeBatchCreate(context, &probeBatch);

    // 2. Add Probes Manually
    for (int i = 0; i < probe_positions.size(); i++) {
        IPLSphere sphere{};
        sphere.center = ResonanceUtils::to_ipl_vector3(probe_positions[i]);
        sphere.radius = 1.0f;
        iplProbeBatchAddProbe(probeBatch, sphere);
    }
    iplProbeBatchCommit(probeBatch);

    if (Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("BAKER: Batch committed with ", (int)probe_positions.size(), " probes. Calculating Reverb...");
    }

    // 3. Bake Params - always bake both so Convolution, Parametric and Hybrid work at runtime without re-bake
    IPLReflectionsBakeParams bakeParams{};
    _fill_reflections_bake_params(bakeParams, scene, probeBatch, scene_type, opencl_device, radeon_rays_device,
        IPL_BAKEDDATAVARIATION_REVERB, num_rays, num_bounces, reflection_type, num_threads);

    // 4. Run Bake (with optional progress callback)
    AdapterData adapter = { progress_callback, progress_user_data };
    iplReflectionsBakerBake(context, &bakeParams,
        (progress_callback && progress_user_data) ? _ipl_progress_adapter : nullptr,
        (progress_callback && progress_user_data) ? &adapter : nullptr);

    // 5. Save to Memory
    IPLSerializedObjectSettings serialSettings{};
    IPLSerializedObject serializedObject = nullptr;
    iplSerializedObjectCreate(context, &serialSettings, &serializedObject);
    iplProbeBatchSave(probeBatch, serializedObject);

    IPLsize size = iplSerializedObjectGetSize(serializedObject);
    if (size == 0) {
        UtilityFunctions::push_error("Nexus Resonance Bake: iplReflectionsBakerBake produced no data. Check scene geometry and probe positions.");
        iplSerializedObjectRelease(&serializedObject);
        iplProbeBatchRelease(&probeBatch);
        return false;
    }
    IPLbyte* data = iplSerializedObjectGetData(serializedObject);

    // 6. Save to Resource
    PackedByteArray pba;
    pba.resize((int64_t)size);
    memcpy(pba.ptrw(), data, size);

    probe_data_res->set_data(pba);
    probe_data_res->set_probe_positions(probe_positions);
    probe_data_res->set_baked_reflection_type(reflection_type);

    // 7. Save to Disk (skip when pathing will run – GDScript saves at end with full pathing_params_hash)
    String path = probe_data_res->get_path();
    if (!path.is_empty() && path.begins_with("uid://")) {
        path = ResourceUID::get_singleton()->uid_to_path(path);
    }
    if (path.is_empty()) {
        int n = s_fallback_counter.fetch_add(1) + 1;
        path = "res://audio_data/baked_probe_data_" + String::num_int64(n) + ".tres";
        UtilityFunctions::push_warning("Nexus Resonance Bake: probe_data has no path. Using fallback: ", path);
        Ref<DirAccess> da = DirAccess::open("res://");
        if (da.is_valid() && !da->dir_exists("audio_data")) {
            da->make_dir("audio_data");
        }
    }
    if (!_save_probe_data_to_disk(probe_data_res, path, pba, reflection_type, size, pathing_scheduled)) {
        iplSerializedObjectRelease(&serializedObject);
        iplProbeBatchRelease(&probeBatch);
        return false;
    }
    if (Engine::get_singleton()->is_editor_hint() && !path.is_empty()) {
        int kb = (int)((size + 512) / 1024);
        UtilityFunctions::print("BAKER: Saved ", kb, " kilobytes to ", path);
    }

    iplSerializedObjectRelease(&serializedObject);
    iplProbeBatchRelease(&probeBatch);

    return true;
}

bool ResonanceBaker::bake_pathing(IPLContext context, IPLScene scene, Ref<ResonanceProbeData> probe_data_res,
    float vis_range, float path_range, int num_samples, float radius, float threshold,
    void (*progress_callback)(float, void*), void* progress_user_data, int num_threads) {
    if (probe_data_res.is_null() || probe_data_res->get_data().is_empty()) {
        UtilityFunctions::push_error("Nexus Resonance: bake_pathing requires probe_data with existing baked reflections.");
        return false;
    }
    if (!context || !scene) {
        UtilityFunctions::push_error("Nexus Resonance: bake_pathing requires initialized context and scene.");
        return false;
    }

    IPLProbeBatch batch = _load_probe_batch_from_resource(context, probe_data_res);
    if (!batch) {
        UtilityFunctions::push_error("Nexus Resonance: bake_pathing failed to load probe batch from probe_data.");
        return false;
    }

    IPLPathBakeParams pathParams{};
    pathParams.scene = scene;
    pathParams.probeBatch = batch;
    pathParams.identifier.type = IPL_BAKEDDATATYPE_PATHING;
    pathParams.identifier.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
    pathParams.numSamples = num_samples;
    pathParams.radius = radius;
    pathParams.threshold = threshold;
    pathParams.visRange = vis_range;
    pathParams.pathRange = path_range;
    pathParams.numThreads = (num_threads < 1) ? 1 : num_threads;

    AdapterData adapter = { progress_callback, progress_user_data };
    iplPathBakerBake(context, &pathParams,
        (progress_callback && progress_user_data) ? _ipl_progress_adapter : nullptr,
        (progress_callback && progress_user_data) ? &adapter : nullptr);

    IPLSerializedObjectSettings serialSettings{};
    IPLSerializedObject serializedObject = nullptr;
    iplSerializedObjectCreate(context, &serialSettings, &serializedObject);
    iplProbeBatchSave(batch, serializedObject);
    iplProbeBatchRelease(&batch);

    IPLsize size = iplSerializedObjectGetSize(serializedObject);
    if (size == 0) {
        UtilityFunctions::push_error("Nexus Resonance: bake_pathing produced no data.");
        iplSerializedObjectRelease(&serializedObject);
        return false;
    }
    IPLbyte* data = iplSerializedObjectGetData(serializedObject);
    PackedByteArray newPba;
    newPba.resize((int64_t)size);
    memcpy(newPba.ptrw(), data, size);
    probe_data_res->set_data(newPba);
    iplSerializedObjectRelease(&serializedObject);

    // Set pathing_params_hash in C++ so it persists even when GDScript's thread-based
    // bake_pathing() return value does not propagate (GDExtension/thread limitation).
    // Hash must match GDScript _compute_pathing_hash: hash(var_to_str({...}))
    {
        Dictionary d;
        d["vis_range"] = vis_range;
        d["path_range"] = path_range;
        d["num_samples"] = num_samples;
        d["radius"] = radius;
        d["threshold"] = threshold;
        String s = UtilityFunctions::var_to_str(d);
        int64_t ph = static_cast<int64_t>(s.hash());
        probe_data_res->set_pathing_params_hash(ph);
    }

    if (Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("Nexus Resonance: Pathing baked successfully.");
    }
    return true;
}

bool ResonanceBaker::bake_static_source(IPLContext context, IPLScene scene, IPLSceneType scene_type,
    IPLOpenCLDevice opencl_device, IPLRadeonRaysDevice radeon_rays_device,
    Ref<ResonanceProbeData> probe_data_res, Vector3 endpoint_position, float influence_radius,
    int num_bounces, int num_rays, void (*progress_callback)(float, void*), void* progress_user_data, int num_threads) {
    if (probe_data_res.is_null() || probe_data_res->get_data().is_empty()) {
        UtilityFunctions::push_error("Nexus Resonance: bake_static_source requires probe_data with existing baked probes (Bake Probes first).");
        return false;
    }
    if (!context || !scene) {
        UtilityFunctions::push_error("Nexus Resonance: bake_static_source requires initialized context and scene.");
        return false;
    }
    if (influence_radius <= 0.0f) influence_radius = 10.0f;

    IPLProbeBatch batch = _load_probe_batch_from_resource(context, probe_data_res);
    if (!batch) {
        UtilityFunctions::push_error("Nexus Resonance: bake_static_source failed to load probe batch.");
        return false;
    }

    int baked_type = probe_data_res->get_baked_reflection_type();
    int refl_type = baked_type >= 0 ? baked_type : 2;
    IPLReflectionsBakeParams bakeParams{};
    _fill_reflections_bake_params(bakeParams, scene, batch, scene_type, opencl_device, radeon_rays_device,
        IPL_BAKEDDATAVARIATION_STATICSOURCE, num_rays, num_bounces, refl_type, num_threads,
        &endpoint_position, influence_radius);

    AdapterData adapter = { progress_callback, progress_user_data };
    iplReflectionsBakerBake(context, &bakeParams,
        (progress_callback && progress_user_data) ? _ipl_progress_adapter : nullptr,
        (progress_callback && progress_user_data) ? &adapter : nullptr);

    IPLSerializedObjectSettings serialSettings{};
    IPLSerializedObject serializedObject = nullptr;
    iplSerializedObjectCreate(context, &serialSettings, &serializedObject);
    iplProbeBatchSave(batch, serializedObject);
    iplProbeBatchRelease(&batch);

    IPLsize size = iplSerializedObjectGetSize(serializedObject);
    if (size == 0) {
        UtilityFunctions::push_error("Nexus Resonance: bake_static_source produced no data.");
        iplSerializedObjectRelease(&serializedObject);
        return false;
    }
    PackedByteArray newPba;
    newPba.resize((int64_t)size);
    memcpy(newPba.ptrw(), iplSerializedObjectGetData(serializedObject), size);
    probe_data_res->set_data(newPba);
    iplSerializedObjectRelease(&serializedObject);

    if (Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("Nexus Resonance: Static source baked successfully.");
    }
    return true;
}

bool ResonanceBaker::bake_static_listener(IPLContext context, IPLScene scene, IPLSceneType scene_type,
    IPLOpenCLDevice opencl_device, IPLRadeonRaysDevice radeon_rays_device,
    Ref<ResonanceProbeData> probe_data_res, Vector3 endpoint_position, float influence_radius,
    int num_bounces, int num_rays, void (*progress_callback)(float, void*), void* progress_user_data, int num_threads) {
    if (probe_data_res.is_null() || probe_data_res->get_data().is_empty()) {
        UtilityFunctions::push_error("Nexus Resonance: bake_static_listener requires probe_data with existing baked probes (Bake Probes first).");
        return false;
    }
    if (!context || !scene) {
        UtilityFunctions::push_error("Nexus Resonance: bake_static_listener requires initialized context and scene.");
        return false;
    }
    if (influence_radius <= 0.0f) influence_radius = 10.0f;

    IPLProbeBatch batch = _load_probe_batch_from_resource(context, probe_data_res);
    if (!batch) {
        UtilityFunctions::push_error("Nexus Resonance: bake_static_listener failed to load probe batch.");
        return false;
    }

    int baked_type = probe_data_res->get_baked_reflection_type();
    int refl_type = baked_type >= 0 ? baked_type : 2;
    IPLReflectionsBakeParams bakeParams{};
    _fill_reflections_bake_params(bakeParams, scene, batch, scene_type, opencl_device, radeon_rays_device,
        IPL_BAKEDDATAVARIATION_STATICLISTENER, num_rays, num_bounces, refl_type, num_threads,
        &endpoint_position, influence_radius);

    AdapterData adapter = { progress_callback, progress_user_data };
    iplReflectionsBakerBake(context, &bakeParams,
        (progress_callback && progress_user_data) ? _ipl_progress_adapter : nullptr,
        (progress_callback && progress_user_data) ? &adapter : nullptr);

    IPLSerializedObjectSettings serialSettings{};
    IPLSerializedObject serializedObject = nullptr;
    iplSerializedObjectCreate(context, &serialSettings, &serializedObject);
    iplProbeBatchSave(batch, serializedObject);
    iplProbeBatchRelease(&batch);

    IPLsize size = iplSerializedObjectGetSize(serializedObject);
    if (size == 0) {
        UtilityFunctions::push_error("Nexus Resonance: bake_static_listener produced no data.");
        iplSerializedObjectRelease(&serializedObject);
        return false;
    }
    PackedByteArray newPba;
    newPba.resize((int64_t)size);
    memcpy(newPba.ptrw(), iplSerializedObjectGetData(serializedObject), size);
    probe_data_res->set_data(newPba);
    iplSerializedObjectRelease(&serializedObject);

    if (Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("Nexus Resonance: Static listener baked successfully.");
    }
    return true;
}
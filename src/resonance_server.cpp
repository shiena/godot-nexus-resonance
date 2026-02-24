#include "resonance_server.h"
#include "resonance_constants.h"
#include "resonance_math.h"
#include "resonance_geometry.h"
#include "resonance_geometry_asset.h"
#include "resonance_material.h"
#include "resonance_debug_agent.h"
#include "resonance_debug_log.h"
#include "ray_trace_debug_context.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#if defined(_WIN32) && defined(_MSC_VER)
#include <excpt.h>
#endif
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <set>

using namespace godot;

namespace {
void bake_progress_callback(float p, void* ud) {
    static_cast<godot::ResonanceServer*>(ud)->emit_bake_progress(p);
}
} // namespace

// ----------------------------------------------------------------------------
// SOURCE MANAGER IMPLEMENTATION
// ----------------------------------------------------------------------------

SourceManager::SourceManager() {}
SourceManager::~SourceManager() {
    std::lock_guard<std::mutex> lock(mutex);
    release_all();
}

int32_t SourceManager::add_source(IPLSource source) {
    if (!source) return -1;
    IPLSource retained_source = iplSourceRetain(source);
    std::lock_guard<std::mutex> lock(mutex);
    int32_t handle = alloc_handle();
    items[handle] = retained_source;
    return handle;
}

void SourceManager::remove_source(int32_t handle) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = items.find(handle);
    if (it != items.end()) {
        _handle_release_source(&(it->second));
        items.erase(it);
        recycle_handle(handle);
    }
}

IPLSource SourceManager::get_source(int32_t handle) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = items.find(handle);
    if (it != items.end()) {
        return iplSourceRetain(it->second);
    }
    return nullptr;
}

void SourceManager::get_all_handles(std::vector<int32_t>& out) {
    std::lock_guard<std::mutex> lock(mutex);
    out.clear();
    for (const auto& pair : items) {
        out.push_back(pair.first);
    }
}

// ----------------------------------------------------------------------------
// PROBE BATCH MANAGER IMPLEMENTATION
// ----------------------------------------------------------------------------

ProbeBatchManager::ProbeBatchManager() {}
ProbeBatchManager::~ProbeBatchManager() {
    std::lock_guard<std::mutex> lock(mutex);
    release_all();
}

int32_t ProbeBatchManager::add_batch(IPLProbeBatch batch) {
    if (!batch) return -1;
    std::lock_guard<std::mutex> lock(mutex);
    int32_t handle = alloc_handle();
    items[handle] = batch;
    return handle;
}

IPLProbeBatch ProbeBatchManager::take_batch(int32_t handle) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = items.find(handle);
    if (it != items.end()) {
        IPLProbeBatch batch = it->second;
        items.erase(it);
        recycle_handle(handle);
        return batch;
    }
    return nullptr;
}

void ProbeBatchManager::get_all_batches(std::vector<IPLProbeBatch>& out) {
    std::lock_guard<std::mutex> lock(mutex);
    clear_and_reset(out);
}

IPLProbeBatch ProbeBatchManager::get_first_batch() {
    std::lock_guard<std::mutex> lock(mutex);
    if (items.empty()) return nullptr;
    return iplProbeBatchRetain(items.begin()->second);
}

IPLProbeBatch ProbeBatchManager::get_batch(int32_t handle) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = items.find(handle);
    if (it != items.end()) return iplProbeBatchRetain(it->second);
    return nullptr;
}

// ----------------------------------------------------------------------------
// RESONANCE SERVER IMPLEMENTATION
// ----------------------------------------------------------------------------

bool ResonanceServer::is_shutting_down_flag = false;
static ResonanceServer* _singleton = nullptr;

void IPLCALL ResonanceServer::_pathing_vis_callback(IPLVector3 from, IPLVector3 to, IPLbool occluded, void* userData) {
    ResonanceServer* server = static_cast<ResonanceServer*>(userData);
    if (!server) return;
    PathVisSegment seg;
    seg.from = ResonanceUtils::to_godot_vector3(from);
    seg.to = ResonanceUtils::to_godot_vector3(to);
    seg.occluded = (occluded == IPL_TRUE);
    std::lock_guard<std::mutex> lock(server->pathing_vis_mutex);
    server->pathing_vis_segments.push_back(seg);
}

void IPLCALL ResonanceServer::_custom_batched_closest_hit(IPLint32 numRays, const IPLRay* rays,
    const IPLfloat32* minDistances, const IPLfloat32* maxDistances, IPLHit* hits, void* userData) {
    ResonanceServer* server = static_cast<ResonanceServer*>(userData);
    if (!server) return;

    int bounce = server->ray_debug_bounce_index_.load(std::memory_order_relaxed);
    server->ray_trace_debug_context_.trace_batch(numRays, rays, minDistances, maxDistances, hits, bounce);
    server->ray_trace_debug_context_.push_rays_for_viz(numRays, rays, hits, bounce);
    server->ray_debug_bounce_index_.store(bounce + 1, std::memory_order_relaxed);
}

void IPLCALL ResonanceServer::_custom_batched_any_hit(IPLint32 numRays, const IPLRay* rays,
    const IPLfloat32* minDistances, const IPLfloat32* maxDistances, IPLuint8* occluded, void* userData) {
    ResonanceServer* server = static_cast<ResonanceServer*>(userData);
    if (!server) return;

    std::vector<IPLHit> hits(numRays);
    int bounce = server->ray_debug_bounce_index_.load(std::memory_order_relaxed);
    server->ray_trace_debug_context_.trace_batch(numRays, rays, minDistances, maxDistances, hits.data(), bounce);
    for (IPLint32 i = 0; i < numRays; i++) {
        occluded[i] = (hits[i].distance < INFINITY) ? 1 : 0;
    }
}

ResonanceServer::ResonanceServer() {
    _singleton = this;
    // No auto-init here!
}

ResonanceServer::~ResonanceServer() {
    is_shutting_down_flag = true;
    _shutdown_steam_audio();
    if (_singleton == this) _singleton = nullptr;
}

ResonanceServer* ResonanceServer::get_singleton() { return _singleton; }

void ResonanceServer::shutdown() {
    is_shutting_down_flag = true;
    _shutdown_steam_audio();
}

// --- PUBLIC CONFIG & INIT ---

void ResonanceServer::_apply_config(Dictionary config) {
    config_.apply(config, [this](const char* key, float def) { return _get_bake_pathing_param(key, def); });
    current_sample_rate = config_.sample_rate;
    frame_size = config_.frame_size;
    ambisonic_order = config_.ambisonic_order;
    max_reverb_duration = config_.max_reverb_duration;
    simulation_threads = config_.simulation_threads;
    simulation_cpu_cores_percent = config_.simulation_cpu_cores_percent;
    max_rays = config_.max_rays;
    max_bounces = config_.max_bounces;
    reverb_influence_radius = config_.reverb_influence_radius;
    reverb_max_distance = config_.reverb_max_distance;
    reverb_transmission_amount = config_.reverb_transmission_amount;
    reflection_type = config_.reflection_type;
    hybrid_reverb_transition_time = config_.hybrid_reverb_transition_time;
    hybrid_reverb_overlap_percent = config_.hybrid_reverb_overlap_percent;
    transmission_type = config_.transmission_type;
    occlusion_type = config_.occlusion_type;
    hrtf_sofa_asset = config_.hrtf_sofa_asset;
    reverb_binaural = config_.reverb_binaural;
    use_virtual_surround = config_.use_virtual_surround;
    hrtf_interpolation_bilinear = config_.hrtf_interpolation_bilinear;
    pathing_enabled = config_.pathing_enabled;
    pathing_vis_radius = config_.pathing_vis_radius;
    pathing_vis_threshold = config_.pathing_vis_threshold;
    pathing_vis_range = config_.pathing_vis_range;
    pathing_normalize_eq = config_.pathing_normalize_eq;
    use_radeon_rays = config_.use_radeon_rays;
    opencl_device_type = config_.opencl_device_type;
    opencl_device_index = config_.opencl_device_index;
    context_validation = config_.context_validation;
    context_simd_level = config_.context_simd_level;
    realtime_irradiance_min_distance = config_.realtime_irradiance_min_distance;
    realtime_simulation_duration = config_.realtime_simulation_duration;
    realtime_num_diffuse_samples = config_.realtime_num_diffuse_samples;
    output_direct_enabled = config_.output_direct_enabled;
    output_reverb_enabled = config_.output_reverb_enabled;
    debug_occlusion = config_.debug_occlusion;
    debug_reflections = config_.debug_reflections;
    debug_pathing = config_.debug_pathing;
    perspective_correction_enabled = config_.perspective_correction_enabled;
    perspective_correction_factor = config_.perspective_correction_factor;
    geometry_update_throttle = config_.geometry_update_throttle;
    simulation_tick_throttle = config_.simulation_tick_throttle;
    simulation_update_interval = config_.simulation_update_interval;
    tick_throttle_counter.store(0, std::memory_order_relaxed);
    simulation_update_time_elapsed = 0.0f;
    reflections_pathing_requested.store(false, std::memory_order_relaxed);
}

void ResonanceServer::init_audio_engine(Dictionary config) {
    if (_ctx() != nullptr) {
        UtilityFunctions::push_warning("Nexus Resonance: Already initialized.");
        return;
    }
    _apply_config(config);
    _init_internal();
}

void ResonanceServer::reinit_audio_engine(Dictionary config) {
    if (_ctx() == nullptr) {
        init_audio_engine(config);
        return;
    }
    _shutdown_steam_audio();
    _apply_config(config);
    _init_internal();
}

void ResonanceServer::_init_internal() {
    reverb_effect_process_calls.store(0, std::memory_order_relaxed);
    reverb_effect_mixer_null.store(0, std::memory_order_relaxed);
    reverb_effect_success.store(0, std::memory_order_relaxed);
    reverb_effect_frames_written.store(0, std::memory_order_relaxed);
    reverb_effect_output_peak.store(0.0f, std::memory_order_relaxed);
    reverb_mixer_feed_count.store(0, std::memory_order_relaxed);
    reverb_convolution_valid_fetches.store(0, std::memory_order_relaxed);
    reverb_convolution_feed_ir_null.store(0, std::memory_order_relaxed);
    reverb_convolution_gain_min.store(1.0f, std::memory_order_relaxed);
    reverb_convolution_gain_max.store(0.0f, std::memory_order_relaxed);
    reverb_convolution_input_rms_max.store(0.0f, std::memory_order_relaxed);

    _init_context_and_devices();
    if (!steam_audio_context_) return;
    _init_scene_and_simulator();
    _start_worker_thread();

    UtilityFunctions::print("Nexus Resonance: Engine Started (Steam Audio 4.8.x). Rate: ", current_sample_rate, " | Order: ", ambisonic_order, " | Rays: ", max_rays);
    Engine* eng = Engine::get_singleton();
    if (max_rays == 0 && _uses_convolution_or_hybrid_or_tan() && (!eng || !eng->is_editor_hint())) {
        UtilityFunctions::push_warning("Nexus Resonance: Rays=0 (Baked Only). Convolution/Hybrid/TAN reverb uses only baked probe data. Bake probes for reverb to work.");
    }
}

void ResonanceServer::_init_context_and_devices() {
    steam_audio_context_ = std::make_unique<ResonanceSteamAudioContext>();
    ResonanceSteamAudioContextConfig ctx_config{};
    ctx_config.sample_rate = current_sample_rate;
    ctx_config.frame_size = frame_size;
    ctx_config.ambisonic_order = ambisonic_order;
    ctx_config.max_reverb_duration = max_reverb_duration;
    ctx_config.reflection_type = reflection_type;
    ctx_config.use_radeon_rays = use_radeon_rays;
    ctx_config.opencl_device_type = opencl_device_type;
    ctx_config.opencl_device_index = opencl_device_index;
    ctx_config.context_validation = context_validation;
    ctx_config.context_simd_level = context_simd_level;
    ctx_config.hrtf_sofa_asset = hrtf_sofa_asset;

    if (!steam_audio_context_->init(ctx_config)) {
        steam_audio_context_.reset();
        return;
    }
    reflection_type = ctx_config.reflection_type;

    if (debug_reflections && max_rays > 0) {
        ray_trace_debug_context_.clear();
        UtilityFunctions::print("Nexus Resonance: Debug Reflections enabled – using Embree + standalone ray viz (no CUSTOM scene).");
    }
}

void ResonanceServer::_init_scene_and_simulator() {
    IPLAudioSettings audioSettings{ current_sample_rate, frame_size };
    IPLSceneSettings sceneSettings{};
    sceneSettings.type = _scene_type();
    sceneSettings.embreeDevice = _embree();
    sceneSettings.radeonRaysDevice = _radeon();
    iplSceneCreate(_ctx(), &sceneSettings, &scene);

    simulation_settings.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    simulation_settings.sceneType = _scene_type();
    simulation_settings.reflectionType =
        (reflection_type == 1) ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC :
        (reflection_type == 2) ? IPL_REFLECTIONEFFECTTYPE_HYBRID :
        (reflection_type == 3) ? IPL_REFLECTIONEFFECTTYPE_TAN :
        IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
    simulation_settings.openCLDevice = _opencl();
    simulation_settings.tanDevice = _tan();
    simulation_settings.maxNumOcclusionSamples = 64;
    simulation_settings.maxNumRays = _uses_convolution_or_hybrid_or_tan() ? (max_rays > 0 ? max_rays : 1) : max_rays;
    simulation_settings.numDiffuseSamples = realtime_num_diffuse_samples;
    simulation_settings.maxDuration = max_reverb_duration;
    simulation_settings.samplingRate = current_sample_rate;
    simulation_settings.frameSize = frame_size;
    simulation_settings.maxOrder = ambisonic_order;
    simulation_settings.numThreads = simulation_threads;
    simulation_settings.maxNumSources = 32;
    simulation_settings.numVisSamples = pathing_enabled ? 16 : 1;
    simulation_settings.rayBatchSize = 1;

    iplSimulatorCreate(_ctx(), &simulation_settings, &simulator);

    if (reflection_type == 0 || reflection_type == 3) {
        IPLReflectionEffectSettings rs{};
        rs.type = (reflection_type == 3) ? IPL_REFLECTIONEFFECTTYPE_TAN : IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
        rs.numChannels = get_num_channels_for_order();
        rs.irSize = (int)(max_reverb_duration * current_sample_rate);
        iplReflectionMixerCreate(_ctx(), &audioSettings, &rs, &reflection_mixer);
    }

    iplSceneCommit(scene);
    iplSimulatorSetScene(simulator, scene);
    iplSimulatorCommit(simulator);

    update_listener(Vector3(0, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));
}

void ResonanceServer::_start_worker_thread() {
    thread_running = true;
    worker_thread = std::thread(&ResonanceServer::_worker_thread_func, this);
}

// --- RUNTIME LOOP ---

void ResonanceServer::tick(float delta) {
    // Accumulate time; Reflections+Pathing only when interval elapsed (reduces CPU)
    simulation_update_time_elapsed += delta;
    if (simulation_update_time_elapsed >= simulation_update_interval) {
        simulation_update_time_elapsed = 0.0f;
        reflections_pathing_requested.store(true, std::memory_order_release);
    }

    if (!_should_run_throttled(tick_throttle_counter, simulation_tick_throttle)) return;
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        simulation_requested = true;
    }
    worker_cv.notify_one();
}

void ResonanceServer::_worker_thread_func() {
    while (thread_running) {
        std::unique_lock<std::mutex> lock(worker_mutex);
        worker_cv.wait(lock, [this] { return simulation_requested || !thread_running; });

        if (!thread_running) break;
        simulation_requested = false;

        IPLCoordinateSpace3 current_listener;
        {
            std::lock_guard<std::mutex> l_lock(listener_mutex);
            current_listener = pending_listener_coords;
            pending_listener_updated = false;
        }
        lock.unlock();

        if (_ctx()) {
            // Skip reflections simulation when no data source: saves CPU, avoids Steam Audio fallback.
            bool run_reflections = (max_rays > 0);
            if (!run_reflections) {
                run_reflections = probe_batch_registry_.has_any_batches();
            }

            std::lock_guard<std::mutex> sim_lock(simulation_mutex);

            // Update shared inputs every tick so numRays (realtime rays) and listener stay current.
            // numRays=0: baked-only (probe interpolation); numRays>0: realtime raytracing.
            IPLSimulationSharedInputs inputs{};
            inputs.listener = current_listener;
            inputs.numRays = max_rays;
            inputs.numBounces = max_bounces;
            inputs.duration = realtime_simulation_duration;
            inputs.order = ambisonic_order;
            inputs.irradianceMinDistance = realtime_irradiance_min_distance;
            inputs.pathingVisCallback = (pathing_enabled && debug_pathing) ? _pathing_vis_callback : nullptr;
            inputs.pathingUserData = (pathing_enabled && debug_pathing) ? static_cast<void*>(this) : nullptr;
            IPLSimulationFlags sim_flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | (run_reflections ? IPL_SIMULATIONFLAGS_REFLECTIONS : 0));
            if (pathing_enabled) sim_flags = static_cast<IPLSimulationFlags>(sim_flags | IPL_SIMULATIONFLAGS_PATHING);
            iplSimulatorSetSharedInputs(simulator, sim_flags, &inputs);

            if (scene_dirty) {
                iplSceneCommit(scene);
                iplSimulatorSetScene(simulator, scene);
                iplSimulatorCommit(simulator);
                scene_dirty = false;
            }

            iplSimulatorRunDirect(simulator);
            // Reflections and Pathing only when simulation_update_interval elapsed (saves CPU)
            pathing_ran_this_tick.store(false);
            bool run_heavy = reflections_pathing_requested.exchange(false, std::memory_order_acq_rel);
            if (run_heavy) {
                if (run_reflections) iplSimulatorRunReflections(simulator);
                // Reduce crash cooldown each tick
                int cooldown = pathing_crash_cooldown.load();
                if (cooldown > 0) pathing_crash_cooldown.store(cooldown - 1);
                if (pathing_enabled && pending_listener_valid.load() && pathing_crash_cooldown.load() <= 0) {
                    if (debug_pathing) {
                        std::lock_guard<std::mutex> pv_lock(pathing_vis_mutex);
                        pathing_vis_segments.clear();
                    }
#if defined(_WIN32) && defined(_MSC_VER)
                    __try {
                        iplSimulatorRunPathing(simulator);
                        pathing_ran_this_tick.store(true);
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) {
                        // Steam Audio RunPathing can crash (SEH) when listener is behind wall / unreachable.
                        // Skip RunPathing for kPathingCrashCooldownTicks to reduce exception storm; avoids hundreds of crashes per second.
                        pathing_crash_cooldown.store(resonance::kPathingCrashCooldownTicks);
                    }
#else
                    iplSimulatorRunPathing(simulator);
                    pathing_ran_this_tick.store(true);
#endif
                }
            }
        }
    }
}

// --- SHUTDOWN ---

void ResonanceServer::_shutdown_steam_audio() {
    if (!_ctx()) return;  // Idempotent; safe to call multiple times
    if (thread_running) {
        thread_running = false;
        worker_cv.notify_all();
        if (worker_thread.joinable()) worker_thread.join();
    }
    {
        std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
        reverb_param_cache_.clear();
    }
    // Clean probe batches (remove from simulator before releasing)
    std::vector<IPLProbeBatch> batches_to_release;
    probe_batch_registry_.get_all_batches_for_shutdown(batches_to_release);
    for (IPLProbeBatch batch : batches_to_release) {
        if (simulator && batch) {
            iplSimulatorRemoveProbeBatch(simulator, batch);
        }
        if (batch) iplProbeBatchRelease(&batch);
    }
    if (simulator && !batches_to_release.empty()) iplSimulatorCommit(simulator);

    if (reflection_mixer) iplReflectionMixerRelease(&reflection_mixer);
    if (simulator) iplSimulatorRelease(&simulator);
    for (IPLStaticMesh m : _runtime_static_meshes) {
        if (m && scene) {
            iplStaticMeshRemove(m, scene);
            iplStaticMeshRelease(&m);
        }
    }
    _runtime_static_meshes.clear();
    if (scene) iplSceneRelease(&scene);
    if (steam_audio_context_) {
        steam_audio_context_->shutdown();
        steam_audio_context_.reset();
    }
}

// --- HELPERS / GETTERS ---

String ResonanceServer::get_version() { return String("Nexus Resonance v") + resonance::kVersion; }
bool ResonanceServer::is_initialized() const { return (_ctx() != nullptr); }
bool ResonanceServer::is_simulating() const { return is_initialized() && global_triangle_count > 0; }
IPLCoordinateSpace3 ResonanceServer::get_current_listener_coords() {
    std::lock_guard<std::mutex> lock(listener_mutex);
    return pending_listener_coords;
}

void ResonanceServer::set_listener_valid(bool valid) {
    pending_listener_valid.store(valid);
}

void ResonanceServer::update_listener(Vector3 pos, Vector3 dir, Vector3 up) {
    if (!_ctx()) return;

    // Orthonormalize basis for safety
    Vector3 dir_n = dir.normalized();
    Vector3 right_n = dir_n.cross(up.normalized()).normalized();
    Vector3 up_n = right_n.cross(dir_n).normalized();

    IPLCoordinateSpace3 listener;
    listener.origin = ResonanceUtils::to_ipl_vector3(pos);
    listener.ahead = ResonanceUtils::to_ipl_vector3(dir_n);
    listener.up = ResonanceUtils::to_ipl_vector3(up_n);
    listener.right = ResonanceUtils::to_ipl_vector3(right_n);

    {
        std::lock_guard<std::mutex> lock(listener_mutex);
        pending_listener_coords = listener;
        pending_listener_updated = true;
    }
}

// --- SOURCE API ---

int32_t ResonanceServer::create_source_handle(Vector3 pos, float radius) {
    if (!_ctx()) return -1;
    IPLSourceSettings settings{};
    settings.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    if (pathing_enabled) settings.flags = static_cast<IPLSimulationFlags>(settings.flags | IPL_SIMULATIONFLAGS_PATHING);
    IPLSource src = nullptr;
    iplSourceCreate(simulator, &settings, &src);
    if (src) {
        iplSourceAdd(src, simulator);
        iplSimulatorCommit(simulator);
        int32_t handle = source_manager.add_source(src);
        _update_source_internal(src, handle, pos, radius, Vector3(0, 0, -1), Vector3(0, 1, 0), 0.0f, 1.0f, true, false, 1.0f,
            false, false, 64, 32, 0, Vector3(0, 0, 0), 0.0f);
        iplSourceRelease(&src);
        return handle;
    }
    return -1;
}

void ResonanceServer::destroy_source_handle(int32_t handle) {
    if (handle < 0 || is_shutting_down_flag || !_ctx()) return;
    IPLSource src = source_manager.get_source(handle);
    if (src) {
        iplSourceRemove(src, simulator);
        iplSourceRelease(&src);
    }
    {
        std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
        reverb_param_cache_.erase(handle);
    }
    {
        std::lock_guard<std::mutex> cb_lock(_attenuation_callback_mutex);
        _source_attenuation_callback_data.erase(handle);
    }
    source_manager.remove_source(handle);
}

void ResonanceServer::update_source(int32_t handle, Vector3 pos, float radius,
    Vector3 source_forward, Vector3 source_up,
    float directivity_weight, float directivity_power, bool air_absorption_enabled,
    bool use_sim_distance_attenuation, float min_distance,
    bool path_validation_enabled, bool find_alternate_paths,
    int occlusion_samples, int num_transmission_rays,
    int baked_data_variation, Vector3 baked_endpoint_center, float baked_endpoint_radius,
    int32_t pathing_probe_batch_handle) {
    if (handle < 0) return;
    IPLSource src = source_manager.get_source(handle);
    if (src) {
        lock_simulation();
        _update_source_internal(src, handle, pos, radius, source_forward, source_up,
            directivity_weight, directivity_power, air_absorption_enabled,
            use_sim_distance_attenuation, min_distance,
            path_validation_enabled, find_alternate_paths,
            occlusion_samples, num_transmission_rays,
            baked_data_variation, baked_endpoint_center, baked_endpoint_radius,
            pathing_probe_batch_handle);
        unlock_simulation();
        iplSourceRelease(&src);
    }
}

void ResonanceServer::set_source_attenuation_callback_data(int32_t handle, int attenuation_mode, float min_distance, float max_distance, const PackedFloat32Array& curve_samples) {
    if (handle < 0) return;
    std::lock_guard<std::mutex> lock(_attenuation_callback_mutex);
    AttenuationCallbackData& d = _source_attenuation_callback_data[handle];
    d.mode = attenuation_mode;
    d.min_distance = min_distance;
    d.max_distance = max_distance;
    d.num_curve_samples = (curve_samples.size() > 0 && curve_samples.size() <= 64) ? curve_samples.size() : 64;
    for (int i = 0; i < d.num_curve_samples && i < curve_samples.size(); i++) {
        d.curve_samples[i] = curve_samples[i];
    }
}

static float IPLCALL _distance_attenuation_callback(IPLfloat32 distance, void* userData) {
    const ResonanceServer::AttenuationCallbackData* d = static_cast<const ResonanceServer::AttenuationCallbackData*>(userData);
    if (!d || d->max_distance <= d->min_distance) return 1.0f;
    if (distance <= d->min_distance) return (d->mode == 2 && d->num_curve_samples > 0) ? resonance::sanitize_audio_float(d->curve_samples[0]) : 1.0f;
    if (distance >= d->max_distance) return (d->mode == 2 && d->num_curve_samples > 0) ? resonance::sanitize_audio_float(d->curve_samples[d->num_curve_samples - 1]) : 0.0f;
    float t = (distance - d->min_distance) / (d->max_distance - d->min_distance);
    t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
    if (d->mode == 1) return 1.0f - t;
    if (d->mode == 2 && d->num_curve_samples > 1) {
        float idx = t * (d->num_curve_samples - 1);
        int i0 = (int)idx;
        int i1 = (i0 + 1 < d->num_curve_samples) ? i0 + 1 : i0;
        float frac = idx - (float)i0;
        float v = d->curve_samples[i0] * (1.0f - frac) + d->curve_samples[i1] * frac;
        return resonance::sanitize_audio_float(v);
    }
    return resonance::sanitize_audio_float(1.0f - t);
}

void ResonanceServer::_update_source_internal(IPLSource src, int32_t handle, Vector3 pos, float radius,
    Vector3 source_forward, Vector3 source_up,
    float directivity_weight, float directivity_power, bool air_absorption_enabled,
    bool use_sim_distance_attenuation, float min_distance,
    bool path_validation_enabled, bool find_alternate_paths,
    int occlusion_samples, int num_transmission_rays,
    int baked_data_variation, Vector3 baked_endpoint_center, float baked_endpoint_radius,
    int32_t pathing_probe_batch_handle) {
    if (!src || !_ctx()) return;
    IPLSimulationInputs inputs{};
    IPLSimulationFlags sim_flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    inputs.directFlags = (IPLDirectSimulationFlags)(IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
    if (use_sim_distance_attenuation) inputs.directFlags = (IPLDirectSimulationFlags)(inputs.directFlags | IPL_DIRECTSIMULATIONFLAGS_DISTANCEATTENUATION);
    if (air_absorption_enabled) inputs.directFlags = (IPLDirectSimulationFlags)(inputs.directFlags | IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION);
    if (directivity_weight != 0.0f || directivity_power != 1.0f) inputs.directFlags = (IPLDirectSimulationFlags)(inputs.directFlags | IPL_DIRECTSIMULATIONFLAGS_DIRECTIVITY);
    inputs.source.origin = ResonanceUtils::to_ipl_vector3(pos);
    inputs.source.ahead = ResonanceUtils::to_ipl_vector3(source_forward.normalized());
    inputs.source.up = ResonanceUtils::to_ipl_vector3(source_up.normalized());
    Vector3 right = source_forward.normalized().cross(source_up.normalized()).normalized();
    inputs.source.right = ResonanceUtils::to_ipl_vector3(right);
    inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
    inputs.directivity.dipoleWeight = directivity_weight;
    inputs.directivity.dipolePower = directivity_power;
    inputs.directivity.callback = nullptr;
    inputs.directivity.userData = nullptr;
    bool use_callback = false;
    AttenuationCallbackData* callback_data = nullptr;
    {
        std::lock_guard<std::mutex> cb_lock(_attenuation_callback_mutex);
        auto it = _source_attenuation_callback_data.find(handle);
        if (it != _source_attenuation_callback_data.end() && (it->second.mode == 1 || it->second.mode == 2)) {
            use_callback = true;
            callback_data = &it->second;
        }
    }
    if (use_sim_distance_attenuation) {
        if (use_callback && callback_data) {
            inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_CALLBACK;
            inputs.distanceAttenuationModel.minDistance = callback_data->min_distance;
            inputs.distanceAttenuationModel.callback = _distance_attenuation_callback;
            inputs.distanceAttenuationModel.userData = callback_data;
        } else {
            inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
            inputs.distanceAttenuationModel.minDistance = min_distance;
            inputs.distanceAttenuationModel.callback = nullptr;
            inputs.distanceAttenuationModel.userData = nullptr;
        }
    } else {
        inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
        inputs.distanceAttenuationModel.callback = nullptr;
        inputs.distanceAttenuationModel.userData = nullptr;
    }
    inputs.occlusionType = (occlusion_type == 0) ? IPL_OCCLUSIONTYPE_RAYCAST : IPL_OCCLUSIONTYPE_VOLUMETRIC;
    inputs.occlusionRadius = radius;
    inputs.numOcclusionSamples = CLAMP(occlusion_samples, 1, simulation_settings.maxNumOcclusionSamples);
    inputs.numTransmissionRays = CLAMP(num_transmission_rays, 1, 256);

    // CRITICAL: Use baked probe data for reflections
    // Variation: 0=REVERB (default), 1=STATICSOURCE, 2=STATICLISTENER
    inputs.baked = IPL_TRUE;
    inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
    if (baked_data_variation == 1) {
        inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_STATICSOURCE;
        inputs.bakedDataIdentifier.endpointInfluence.center = ResonanceUtils::to_ipl_vector3(baked_endpoint_center);
        inputs.bakedDataIdentifier.endpointInfluence.radius = (baked_endpoint_radius > 0.0f) ? baked_endpoint_radius : reverb_influence_radius;
    } else if (baked_data_variation == 2) {
        inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_STATICLISTENER;
        inputs.bakedDataIdentifier.endpointInfluence.center = ResonanceUtils::to_ipl_vector3(baked_endpoint_center);
        inputs.bakedDataIdentifier.endpointInfluence.radius = (baked_endpoint_radius > 0.0f) ? baked_endpoint_radius : reverb_influence_radius;
    } else {
        inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
        // Reverb is a global bake - identifier must use empty sphere to match baked data.
        // See Steam Audio core itest_bakedindirect.cpp.
        inputs.bakedDataIdentifier.endpointInfluence.center = { 0.0f, 0.0f, 0.0f };
        inputs.bakedDataIdentifier.endpointInfluence.radius = 0.0f;
    }

    inputs.reverbScale[0] = 1.0f;
    inputs.reverbScale[1] = 1.0f;
    inputs.reverbScale[2] = 1.0f;
    inputs.hybridReverbTransitionTime = hybrid_reverb_transition_time;
    inputs.hybridReverbOverlapPercent = hybrid_reverb_overlap_percent;

    if (pathing_enabled) {
        IPLProbeBatch path_batch = _get_pathing_batch_for_source(pathing_probe_batch_handle);
        if (path_batch) {
            sim_flags = static_cast<IPLSimulationFlags>(sim_flags | IPL_SIMULATIONFLAGS_PATHING);
            inputs.pathingProbes = path_batch;
            inputs.pathingOrder = ambisonic_order;
            inputs.visRadius = pathing_vis_radius;
            inputs.visThreshold = pathing_vis_threshold;
            inputs.visRange = pathing_vis_range;
            // enableValidation/findAlternatePaths control path-around-obstacles (sound around corners).
            // Do NOT tie them to listener_valid—that would disable pathing whenever listener might be
            // unreachable. RunPathing crash (listener in geometry) is handled by SEH in the worker.
            inputs.enableValidation = path_validation_enabled ? IPL_TRUE : IPL_FALSE;
            inputs.findAlternatePaths = find_alternate_paths ? IPL_TRUE : IPL_FALSE;
            {
                std::lock_guard<std::mutex> d_lock(_pathing_deviation_mutex);
                inputs.deviationModel = (_pathing_deviation_callback_enabled && _pathing_deviation_model.callback) ? &_pathing_deviation_model : nullptr;
            }
            iplProbeBatchRelease(&path_batch);
        }
    }
    inputs.flags = sim_flags;

    iplSourceSetInputs(src, sim_flags, &inputs);
}

IPLSource ResonanceServer::get_source_from_handle(int32_t handle) {
    return source_manager.get_source(handle);
}

// --- CALCULATIONS ---

float ResonanceServer::calculate_distance_attenuation(Vector3 source_pos, Vector3 listener_pos, float min_dist, float max_dist) {
    if (!_ctx()) return 1.0f;
    IPLDistanceAttenuationModel model{};
    model.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
    model.minDistance = min_dist;
    IPLVector3 src = ResonanceUtils::to_ipl_vector3(source_pos);
    IPLVector3 dst = ResonanceUtils::to_ipl_vector3(listener_pos);
    return iplDistanceAttenuationCalculate(_ctx(), src, dst, &model);
}

Vector3 ResonanceServer::calculate_air_absorption(Vector3 source_pos, Vector3 listener_pos) {
    if (!_ctx()) return Vector3(1, 1, 1);
    IPLAirAbsorptionModel model{};
    model.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
    IPLVector3 src = ResonanceUtils::to_ipl_vector3(source_pos);
    IPLVector3 dst = ResonanceUtils::to_ipl_vector3(listener_pos);
    float air_abs[3] = { 1.0f, 1.0f, 1.0f };
    iplAirAbsorptionCalculate(_ctx(), src, dst, &model, air_abs);
    return Vector3(air_abs[0], air_abs[1], air_abs[2]);
}

float ResonanceServer::calculate_directivity(Vector3 source_pos, Vector3 fwd, Vector3 up, Vector3 right, Vector3 listener_pos, float weight, float power) {
    if (!_ctx()) return 1.0f;
    IPLDirectivity dSettings{};
    dSettings.dipoleWeight = weight;
    dSettings.dipolePower = power;
    IPLCoordinateSpace3 source_space{};
    source_space.origin = ResonanceUtils::to_ipl_vector3(source_pos);
    source_space.ahead = ResonanceUtils::to_ipl_vector3(fwd);
    source_space.up = ResonanceUtils::to_ipl_vector3(up);
    source_space.right = ResonanceUtils::to_ipl_vector3(right);
    IPLVector3 lst = ResonanceUtils::to_ipl_vector3(listener_pos);
    return iplDirectivityCalculate(_ctx(), source_space, lst, &dSettings);
}

// --- DATA FETCH ---

OcclusionData ResonanceServer::get_source_occlusion_data(int32_t handle) {
    OcclusionData result;
    result.occlusion = 0.0f;
    result.transmission[0] = 1.0f; result.transmission[1] = 1.0f; result.transmission[2] = 1.0f;
    result.air_absorption[0] = 1.0f; result.air_absorption[1] = 1.0f; result.air_absorption[2] = 1.0f;
    result.directivity = 1.0f;
    result.distance_attenuation = 1.0f;
    if (handle < 0 || !_ctx()) return result;
    IPLSource src = source_manager.get_source(handle);
    if (!src) return result;

    {
        std::lock_guard<std::mutex> lock(simulation_mutex);
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
        result.occlusion = outputs.direct.occlusion;
        result.transmission[0] = outputs.direct.transmission[0];
        result.transmission[1] = outputs.direct.transmission[1];
        result.transmission[2] = outputs.direct.transmission[2];
        result.air_absorption[0] = outputs.direct.airAbsorption[0];
        result.air_absorption[1] = outputs.direct.airAbsorption[1];
        result.air_absorption[2] = outputs.direct.airAbsorption[2];
        result.directivity = outputs.direct.directivity;
        result.distance_attenuation = outputs.direct.distanceAttenuation;
    }
    iplSourceRelease(&src);
    return result;
}

bool ResonanceServer::fetch_reverb_params(int32_t handle, IPLReflectionEffectParams& out_params) {
    if (handle < 0 || !_ctx()) return false;
    IPLSource src = source_manager.get_source(handle);
    if (!src) return false;
    bool result = false;

    // Steam Audio can return non-zero reverb times even with no probes and numRays=0 (e.g. scene-based
    // estimate or internal default). For reliable output: only treat as valid when we have a real
    // data source (probe batches for baked, or realtime rays).
    if (_uses_parametric_or_hybrid() || reflection_type == 3) {
        if (max_rays == 0) {
            if (!probe_batch_registry_.has_any_batches()) {
                iplSourceRelease(&src);
                return false;
            }
        }
    }

    // Convolution (0), Hybrid (2), TAN (3) need IR/slot + full params every frame; use blocking lock.
    bool got_lock = false;
    if (_uses_convolution_or_hybrid_or_tan()) {
        simulation_mutex.lock();
        got_lock = true;
    } else {
        got_lock = simulation_mutex.try_lock();
    }
    if (got_lock) {
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
        bool has_convolution = (outputs.reflections.ir != nullptr);
        bool has_parametric = (outputs.reflections.reverbTimes[0] > 0 || outputs.reflections.reverbTimes[1] > 0 || outputs.reflections.reverbTimes[2] > 0);
        bool has_hybrid = (reflection_type == 2 && (has_convolution || outputs.reflections.reverbTimes[0] > 0));
        bool has_tan = (reflection_type == 3 && outputs.reflections.tanSlot >= 0 && _tan());
        if (has_convolution || (reflection_type == 1 && has_parametric) || has_hybrid || has_tan) {
            out_params = outputs.reflections;
            for (int i = 0; i < 3; i++) {
                out_params.reverbTimes[i] = resonance::sanitize_audio_float(out_params.reverbTimes[i]);
                out_params.eq[i] = resonance::sanitize_audio_float(out_params.eq[i]);
            }
            out_params.delay = resonance::sanitize_audio_float(out_params.delay);
            // For Hybrid: Steam Audio validation requires ir non-null when type=HYBRID. Use PARAMETRIC when no IR.
            bool use_hybrid_type = (reflection_type == 2 && has_convolution && has_parametric);
            out_params.type = (reflection_type == 1) ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC :
                (reflection_type == 2 && use_hybrid_type) ? IPL_REFLECTIONEFFECTTYPE_HYBRID :
                (reflection_type == 2) ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC :
                (reflection_type == 3) ? IPL_REFLECTIONEFFECTTYPE_TAN : IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
            if (reflection_type == 3) out_params.tanDevice = _tan();
            result = true;
            if (reflection_type == 0 && has_convolution) {
                reverb_convolution_valid_fetches.fetch_add(1, std::memory_order_relaxed);
            }
            if (reflection_type == 1 && has_parametric) {
                std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
                for (int i = 0; i < 3; i++) {
                    reverb_param_cache_[handle].reverbTimes[i] = outputs.reflections.reverbTimes[i];
                    reverb_param_cache_[handle].eq[i] = outputs.reflections.eq[i];
                }
                reverb_param_cache_[handle].valid = true;
            }
        }
        simulation_mutex.unlock();
    } else if (reflection_type == 1) {
        std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
        auto it = reverb_param_cache_.find(handle);
        if (it != reverb_param_cache_.end() && it->second.valid) {
            memset(&out_params, 0, sizeof(out_params));
            out_params.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
            for (int i = 0; i < 3; i++) {
                out_params.reverbTimes[i] = resonance::sanitize_audio_float(it->second.reverbTimes[i]);
                out_params.eq[i] = resonance::sanitize_audio_float(it->second.eq[i]);
            }
            result = true;
        }
    }

    iplSourceRelease(&src);
    return result;
}

bool ResonanceServer::fetch_pathing_params(int32_t handle, IPLPathEffectParams& out_params) {
    if (handle < 0 || !_ctx() || !pathing_enabled) return false;
    IPLSource src = source_manager.get_source(handle);
    if (!src) return false;
    bool result = false;
    {
        std::lock_guard<std::mutex> lock(simulation_mutex);
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_PATHING, &outputs);
        if (outputs.pathing.shCoeffs != nullptr) {
            out_params = outputs.pathing;
            out_params.binaural = reverb_binaural ? IPL_TRUE : IPL_FALSE;
            out_params.hrtf = _hrtf();
            out_params.normalizeEQ = pathing_normalize_eq ? IPL_TRUE : IPL_FALSE;
            result = true;
        } else {
            if (!pathing_no_data_warned) {
                pathing_no_data_warned = true;
                UtilityFunctions::push_warning("Nexus Resonance: Pathing enabled but no pathing data. Bake Pathing separately after Bake Probes (Editor: select ProbeVolume -> Bake Pathing).");
            }
        }
    }
    iplSourceRelease(&src);
    return result;
}

void ResonanceServer::set_pathing_deviation_callback(IPLDeviationCallback callback, void* userData) {
    std::lock_guard<std::mutex> lock(_pathing_deviation_mutex);
    if (callback) {
        _pathing_deviation_model.type = IPL_DEVIATIONTYPE_CALLBACK;
        _pathing_deviation_model.callback = callback;
        _pathing_deviation_model.userData = userData;
        _pathing_deviation_callback_enabled = true;
    } else {
        _pathing_deviation_model.type = IPL_DEVIATIONTYPE_DEFAULT;
        _pathing_deviation_model.callback = nullptr;
        _pathing_deviation_model.userData = nullptr;
        _pathing_deviation_callback_enabled = false;
    }
}

void ResonanceServer::clear_pathing_deviation_callback() {
    set_pathing_deviation_callback(nullptr, nullptr);
}

// --- SCENE I/O ---

void ResonanceServer::notify_geometry_changed(int triangle_delta) {
    if (!_ctx()) return;
    bool should_mark_dirty = (triangle_delta != 0) || _should_run_throttled(geometry_update_throttle_counter, geometry_update_throttle);
    std::lock_guard<std::mutex> lock(simulation_mutex);
    global_triangle_count += triangle_delta;
    if (should_mark_dirty) scene_dirty = true;
}

void ResonanceServer::save_scene_data(String filename) {
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.save_scene_data(_ctx(), scene, filename);
}

void ResonanceServer::save_scene_obj(String file_base_name) {
    std::lock_guard<std::mutex> lock(simulation_mutex);
    if (!scene) {
        UtilityFunctions::push_warning("Nexus Resonance: No scene to export (save_scene_obj).");
        return;
    }
    String abs_path = file_base_name;
    if (file_base_name.begins_with("res://") || file_base_name.begins_with("user://")) {
        ProjectSettings* ps = ProjectSettings::get_singleton();
        if (ps) abs_path = ps->globalize_path(file_base_name);
    }
    CharString path = abs_path.utf8();
    iplSceneSaveOBJ(scene, path.get_data());
    if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("Nexus Resonance: Scene exported to OBJ (base: ", file_base_name, ").");
    }
}

void ResonanceServer::load_scene_data(String filename) {
    if (!_ctx() || !simulator) return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.load_scene_data(_ctx(), &scene, simulator, _scene_type(), _embree(), _radeon(), filename, &global_triangle_count);
}

Error ResonanceServer::export_static_scene_to_asset(Node* scene_root, const String& p_path) {
    return scene_manager_.export_static_scene_to_asset(scene_root, p_path);
}

int64_t ResonanceServer::get_static_scene_hash(Node* scene_root) {
    return scene_manager_.get_static_scene_hash(scene_root, [this](const PackedByteArray& pba) { return _hash_probe_data(pba); });
}

// --- BAKING & PROBES ---

uint64_t ResonanceServer::_hash_probe_data(const PackedByteArray& pba) {
    uint64_t h = 14695981039346656037ULL; // FNV offset basis
    const uint8_t* ptr = pba.ptr();
    int64_t len = pba.size();
    for (int64_t i = 0; i < len; i++) {
        h ^= (uint64_t)ptr[i];
        h *= 1099511628211ULL; // FNV prime
    }
    return h;
}

PackedVector3Array ResonanceServer::generate_manual_grid(const Transform3D& tr, Vector3 extents, float spacing,
    int generation_type, float height_above_floor) {
    return baker.generate_manual_grid(tr, extents, spacing, generation_type, height_above_floor);
}

PackedVector3Array ResonanceServer::generate_probes_scene_aware(const Transform3D& volume_transform, Vector3 extents,
    float spacing, int generation_type, float height_above_floor) {
    PackedVector3Array out;
    if (!_ctx()) return out;
    if (generation_type != 0 && generation_type != 1) return out;  // GEN_CENTROID=0, GEN_UNIFORM_FLOOR=1
    std::lock_guard<std::mutex> lock(simulation_mutex);
    if (scene_dirty) {
        iplSceneCommit(scene);
        scene_dirty = false;
    }
    IPLScene temp_scene = nullptr;
    IPLStaticMesh temp_mesh = nullptr;
    IPLScene bake_scene = _prepare_bake_scene(&temp_scene, &temp_mesh);
    if (!bake_scene) return out;
    IPLProbeArray probeArray = nullptr;
    if (iplProbeArrayCreate(_ctx(), &probeArray) != IPL_STATUS_SUCCESS) return out;
    IPLProbeGenerationParams genParams{};
    genParams.type = (generation_type == 0) ? IPL_PROBEGENERATIONTYPE_CENTROID : IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR;
    genParams.spacing = spacing;
    genParams.height = height_above_floor;
    genParams.transform = ResonanceUtils::create_volume_transform_rotated(volume_transform, extents);
    iplProbeArrayGenerateProbes(probeArray, bake_scene, &genParams);
    int num_probes = iplProbeArrayGetNumProbes(probeArray);
    for (int i = 0; i < num_probes; i++) {
        IPLSphere sphere = iplProbeArrayGetProbe(probeArray, i);
        out.push_back(ResonanceUtils::to_godot_vector3(sphere.center));
    }
    iplProbeArrayRelease(&probeArray);
    if (temp_mesh) {
        iplStaticMeshRemove(temp_mesh, temp_scene);
        iplStaticMeshRelease(&temp_mesh);
    }
    if (temp_scene) iplSceneRelease(&temp_scene);
    return out;
}

void ResonanceServer::set_bake_pipeline_pathing(bool p_pathing) {
    _bake_pipeline_pathing = p_pathing;
}

void ResonanceServer::set_bake_static_scene_asset(const Ref<ResonanceGeometryAsset>& p_asset) {
    _bake_static_scene_asset = p_asset;
}

void ResonanceServer::clear_static_scenes() {
    if (!_ctx() || !scene) return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.clear_static_scenes(scene, &ray_trace_debug_context_, _runtime_static_meshes,
        _runtime_static_triangle_count, _runtime_static_debug_mesh_ids, &global_triangle_count, &scene_dirty);
}

void ResonanceServer::add_static_scene_from_asset(const Ref<ResonanceGeometryAsset>& p_asset) {
    if (!_ctx() || !scene) return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.add_static_scene_from_asset(_ctx(), scene, p_asset, &ray_trace_debug_context_,
        wants_debug_reflection_viz(), _runtime_static_meshes, _runtime_static_triangle_count,
        _runtime_static_debug_mesh_ids, &global_triangle_count, &scene_dirty);
}

void ResonanceServer::load_static_scene_from_asset(const Ref<ResonanceGeometryAsset>& p_asset) {
    if (!_ctx() || !scene) return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.load_static_scene_from_asset(_ctx(), scene, p_asset, &ray_trace_debug_context_,
        wants_debug_reflection_viz(), _runtime_static_meshes, _runtime_static_triangle_count,
        _runtime_static_debug_mesh_ids, &global_triangle_count, &scene_dirty);
}

void ResonanceServer::set_bake_params(Dictionary params) {
    if (params.has("bake_num_rays")) _bake_num_rays = (int)params["bake_num_rays"];
    if (params.has("bake_num_bounces")) _bake_num_bounces = (int)params["bake_num_bounces"];
    if (params.has("bake_num_threads")) _bake_num_threads = (int)params["bake_num_threads"];
    if (params.has("bake_reflection_type")) _bake_reflection_type = (int)params["bake_reflection_type"];
    if (params.has("bake_pathing_vis_range")) _bake_pathing_vis_range = (float)params["bake_pathing_vis_range"];
    if (params.has("bake_pathing_path_range")) _bake_pathing_path_range = (float)params["bake_pathing_path_range"];
    if (params.has("bake_pathing_num_samples")) _bake_pathing_num_samples = (int)params["bake_pathing_num_samples"];
    if (params.has("bake_pathing_radius")) _bake_pathing_radius = (float)params["bake_pathing_radius"];
    if (params.has("bake_pathing_threshold")) _bake_pathing_threshold = (float)params["bake_pathing_threshold"];
}

int ResonanceServer::_get_bake_num_rays() const {
    if (_bake_num_rays >= 0) return _bake_num_rays;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps) return (int)ps->get_setting("audio/nexus_resonance/bake_num_rays", resonance::kBakeDefaultNumRays);
    return resonance::kBakeDefaultNumRays;
}

int ResonanceServer::_get_bake_num_bounces() const {
    if (_bake_num_bounces >= 0) return _bake_num_bounces;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps) return (int)ps->get_setting("audio/nexus_resonance/bake_num_bounces", resonance::kBakeDefaultNumBounces);
    return resonance::kBakeDefaultNumBounces;
}

int ResonanceServer::_get_bake_num_threads() const {
    if (_bake_num_threads >= 0) return _bake_num_threads;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps) return (int)ps->get_setting("audio/nexus_resonance/bake_num_threads", resonance::kBakeDefaultNumThreads);
    return resonance::kBakeDefaultNumThreads;
}

int ResonanceServer::_get_bake_reflection_type() const {
    if (_bake_reflection_type >= 0) return _bake_reflection_type;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps) return (int)ps->get_setting("audio/nexus_resonance/bake_reflection_type", 2);
    return 2;
}

float ResonanceServer::_get_bake_pathing_param(const char* key, float default_val) const {
    if (strcmp(key, "bake_pathing_vis_range") == 0 && _bake_pathing_vis_range >= 0) return _bake_pathing_vis_range;
    if (strcmp(key, "bake_pathing_path_range") == 0 && _bake_pathing_path_range >= 0) return _bake_pathing_path_range;
    if (strcmp(key, "bake_pathing_radius") == 0 && _bake_pathing_radius >= 0) return _bake_pathing_radius;
    if (strcmp(key, "bake_pathing_threshold") == 0 && _bake_pathing_threshold >= 0) return _bake_pathing_threshold;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (!ps) return default_val;
    String path = String("audio/nexus_resonance/") + key;
    return (float)ps->get_setting(path, default_val);
}

int ResonanceServer::_get_bake_pathing_num_samples() const {
    if (_bake_pathing_num_samples >= 0) return _bake_pathing_num_samples;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps) return (int)ps->get_setting("audio/nexus_resonance/bake_pathing_num_samples", resonance::kBakePathingDefaultNumSamples);
    return resonance::kBakePathingDefaultNumSamples;
}

IPLScene ResonanceServer::_prepare_bake_scene(IPLScene* out_temp_scene, IPLStaticMesh* out_temp_mesh) {
    *out_temp_scene = nullptr;
    *out_temp_mesh = nullptr;
    if (_bake_static_scene_asset.is_valid() && _bake_static_scene_asset->is_valid() && _ctx()) {
        IPLSceneSettings sceneSettings{};
        sceneSettings.type = _scene_type();
        sceneSettings.embreeDevice = _embree();
        sceneSettings.radeonRaysDevice = _radeon();
        IPLScene temp = nullptr;
        if (iplSceneCreate(_ctx(), &sceneSettings, &temp) != IPL_STATUS_SUCCESS) return scene;
        IPLSerializedObjectSettings serialSettings{};
        serialSettings.data = const_cast<IPLbyte*>(reinterpret_cast<const IPLbyte*>(_bake_static_scene_asset->get_data_ptr()));
        serialSettings.size = static_cast<IPLsize>(_bake_static_scene_asset->get_size());
        IPLSerializedObject serialObj = nullptr;
        if (iplSerializedObjectCreate(_ctx(), &serialSettings, &serialObj) != IPL_STATUS_SUCCESS) {
            iplSceneRelease(&temp);
            return scene;
        }
        IPLStaticMesh loadMesh = nullptr;
        if (iplStaticMeshLoad(temp, serialObj, nullptr, nullptr, &loadMesh) != IPL_STATUS_SUCCESS) {
            iplSerializedObjectRelease(&serialObj);
            iplSceneRelease(&temp);
            return scene;
        }
        iplSerializedObjectRelease(&serialObj);
        iplStaticMeshAdd(loadMesh, temp);
        iplSceneCommit(temp);
        *out_temp_scene = temp;
        *out_temp_mesh = loadMesh;
        return temp;
    }
    return scene;
}

bool ResonanceServer::_should_run_throttled(std::atomic<uint32_t>& counter, int throttle) {
    if (throttle <= 1) return true;
    uint32_t c = counter.fetch_add(1, std::memory_order_relaxed);
    return (c % (uint32_t)throttle) == 0;
}

bool ResonanceServer::_with_bake_scene(std::function<bool(IPLScene)> bake_fn) {
    std::lock_guard<std::mutex> lock(simulation_mutex);
    if (scene_dirty) {
        iplSceneCommit(scene);
        scene_dirty = false;
    }
    IPLScene temp_scene = nullptr;
    IPLStaticMesh temp_mesh = nullptr;
    IPLScene bake_scene = _prepare_bake_scene(&temp_scene, &temp_mesh);
    bool ok = bake_fn(bake_scene);
    if (temp_mesh) {
        iplStaticMeshRemove(temp_mesh, temp_scene);
        iplStaticMeshRelease(&temp_mesh);
    }
    if (temp_scene) iplSceneRelease(&temp_scene);
    return ok;
}

bool ResonanceServer::bake_manual_grid(const PackedVector3Array& points, Ref<ResonanceProbeData> data) {
    if (!_ctx() || !scene) {
        UtilityFunctions::push_error("Nexus Resonance Bake: Server not initialized.");
        return false;
    }
    if (!_bake_static_scene_asset.is_valid() || !_bake_static_scene_asset->is_valid()) {
        if (global_triangle_count <= 0) {
            UtilityFunctions::push_error("Nexus Resonance Bake: Scene not exported. Use Tools > Nexus Resonance > Export Static Scene before baking.");
            return false;
        }
    }
    int nb = _get_bake_num_bounces();
    int nr = _get_bake_num_rays();
    int bake_reflection = _get_bake_reflection_type();
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, &points, &data, nb, nr, bake_reflection, nt](IPLScene bake_scene) {
        return baker.bake_manual_grid(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(), points, nb, nr, bake_reflection, data, bake_progress_callback, this, _bake_pipeline_pathing, nt);
    });
}

bool ResonanceServer::bake_probes_for_volume(const Transform3D& volume_transform, Vector3 extents, float spacing,
    int generation_type, float height_above_floor, Ref<ResonanceProbeData> probe_data_res) {
    if (!_ctx() || !scene) {
        UtilityFunctions::push_error("Nexus Resonance Bake: Server not initialized.");
        return false;
    }
    if (!_bake_static_scene_asset.is_valid() || !_bake_static_scene_asset->is_valid()) {
        if (global_triangle_count <= 0) {
            UtilityFunctions::push_error("Nexus Resonance Bake: Scene not exported. Use Tools > Nexus Resonance > Export Static Scene before baking.");
            return false;
        }
    }
    int nb = _get_bake_num_bounces();
    int nr = _get_bake_num_rays();
    int bake_reflection = _get_bake_reflection_type();
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, volume_transform, extents, spacing, generation_type, height_above_floor, probe_data_res, nb, nr, bake_reflection, nt](IPLScene bake_scene) {
        if (generation_type == 0 || generation_type == 1) {
            return baker.bake_with_probe_array(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(),
                volume_transform, extents, spacing, generation_type, height_above_floor,
                nb, nr, bake_reflection, probe_data_res, bake_progress_callback, this, _bake_pipeline_pathing, nt);
        }
        PackedVector3Array points = baker.generate_manual_grid(volume_transform, extents, spacing, generation_type, height_above_floor);
        if (points.size() == 0) return false;
        return baker.bake_manual_grid(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(), points, nb, nr, bake_reflection, probe_data_res, bake_progress_callback, this, _bake_pipeline_pathing, nt);
    });
}

bool ResonanceServer::bake_pathing(Ref<ResonanceProbeData> data) {
    if (!_ctx() || !scene) return false;
    if (data.is_null() || data->get_data().is_empty()) return false;
    float vis_range = _get_bake_pathing_param("bake_pathing_vis_range", resonance::kBakePathingDefaultVisRange);
    float path_range = _get_bake_pathing_param("bake_pathing_path_range", resonance::kBakePathingDefaultPathRange);
    int num_samples = _get_bake_pathing_num_samples();
    float radius = _get_bake_pathing_param("bake_pathing_radius", resonance::kBakePathingDefaultRadius);
    float threshold = _get_bake_pathing_param("bake_pathing_threshold", resonance::kBakePathingDefaultThreshold);
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, data, vis_range, path_range, num_samples, radius, threshold, nt](IPLScene bake_scene) {
        return baker.bake_pathing(_ctx(), bake_scene, data, vis_range, path_range, num_samples, radius, threshold, bake_progress_callback, this, nt);
    });
}

bool ResonanceServer::bake_static_source(Ref<ResonanceProbeData> data, Vector3 endpoint_position, float influence_radius) {
    if (!_ctx() || !scene) return false;
    if (data.is_null() || data->get_data().is_empty()) return false;
    int nb = _get_bake_num_bounces();
    int nr = _get_bake_num_rays();
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, data, endpoint_position, influence_radius, nb, nr, nt](IPLScene bake_scene) {
        return baker.bake_static_source(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(),
            data, endpoint_position, influence_radius, nb, nr, bake_progress_callback, this, nt);
    });
}

bool ResonanceServer::bake_static_listener(Ref<ResonanceProbeData> data, Vector3 endpoint_position, float influence_radius) {
    if (!_ctx() || !scene) return false;
    if (data.is_null() || data->get_data().is_empty()) return false;
    int nb = _get_bake_num_bounces();
    int nr = _get_bake_num_rays();
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, data, endpoint_position, influence_radius, nb, nr, nt](IPLScene bake_scene) {
        return baker.bake_static_listener(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(),
            data, endpoint_position, influence_radius, nb, nr, bake_progress_callback, this, nt);
    });
}

void ResonanceServer::cancel_reflections_bake() {
    if (_ctx()) iplReflectionsBakerCancelBake(_ctx());
}

void ResonanceServer::cancel_pathing_bake() {
    if (_ctx()) iplPathBakerCancelBake(_ctx());
}

int32_t ResonanceServer::load_probe_batch(Ref<ResonanceProbeData> data) {
    if (!_ctx() || data.is_null()) {
        UtilityFunctions::push_warning("Nexus Resonance: load_probe_batch skipped (no context or null data)");
        return -1;
    }
    PackedByteArray pba = data->get_data();
    if (pba.is_empty()) {
        UtilityFunctions::push_warning("Nexus Resonance: load_probe_batch skipped - probe_data.data is empty! Re-bake the probes.");
        return -1;
    }

    int baked_type = data->get_baked_reflection_type();
    if (baked_type >= 0 && baked_type <= 2) {
        if (!_is_reflection_type_compatible(baked_type)) {
            static std::set<std::pair<int, int>> s_warned_mismatch;
            auto key = std::make_pair(baked_type, reflection_type);
            if (s_warned_mismatch.find(key) == s_warned_mismatch.end()) {
                s_warned_mismatch.insert(key);
                const char* baked_names[] = {"Convolution", "Parametric", "Hybrid"};
                const char* runt_names[] = {"Convolution", "Parametric", "Hybrid", "TrueAudio Next"};
                String err = String("Probe data was baked as ") + baked_names[baked_type] +
                    " but runtime is set to " + runt_names[(reflection_type >= 0 && reflection_type <= 3) ? reflection_type : 0] +
                    ". Re-bake probes with matching reflection type or change runtime ReflectionType to match.";
                UtilityFunctions::push_error(err);
                if (Engine::get_singleton()->has_singleton("ResonanceLogger")) {
                    Dictionary log_data;
                    log_data["baked_reflection_type"] = baked_type;
                    log_data["runtime_reflection_type"] = reflection_type;
                    resonance_logger_log("validation", err.utf8().get_data(), log_data);
                }
            }
            return -1;
        }
    }

    // Skip pathing validation when called from bake_probes() mid-pipeline: pathing is baked
    // in a later step, so hash is 0 at this point. Validation runs again on reload after save.
    bool skip_pathing_check = _bake_pipeline_pathing;
    if (!skip_pathing_check && pathing_enabled && data->get_pathing_params_hash() == 0) {
        static bool s_warned_pathing_mismatch = false;
        if (!s_warned_pathing_mismatch) {
            s_warned_pathing_mismatch = true;
            String err = "Nexus Resonance: Pathing is enabled but probe data has no pathing baked. Bake Pathing in the Probe Volume editor (enable Pathing in bake_config, then Bake).";
            UtilityFunctions::push_warning(err);
            if (Engine::get_singleton()->has_singleton("ResonanceLogger")) {
                Dictionary log_data;
                log_data["pathing_enabled"] = true;
                log_data["pathing_params_hash"] = 0;
                resonance_logger_log("validation", err.utf8().get_data(), log_data);
            }
        }
        return -1;
    }

    uint64_t data_hash = _hash_probe_data(pba);
    auto is_refl_compat = [this](int baked) { return _is_reflection_type_compatible(baked); };
    int32_t handle = probe_batch_registry_.load_batch(_ctx(), simulator, &simulation_mutex, data, data_hash,
        reflection_type, pathing_enabled, _bake_pipeline_pathing, is_refl_compat);
    return handle;
}

void ResonanceServer::remove_probe_batch(int32_t handle) {
    if (handle < 0 || is_shutting_down_flag || !_ctx() || !simulator) return;
    probe_batch_registry_.remove_batch(handle, simulator, &simulation_mutex);
    std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
    reverb_param_cache_.clear();
}

IPLProbeBatch ResonanceServer::_get_first_batch_with_pathing() {
    return _get_pathing_batch_for_source(-1);
}

IPLProbeBatch ResonanceServer::_get_pathing_batch_for_source(int32_t preferred_handle) {
    return probe_batch_registry_.get_pathing_batch(preferred_handle);
}

bool ResonanceServer::_uses_convolution_or_hybrid_or_tan() const {
    return reflection_type == 0 || reflection_type == 2 || reflection_type == 3;
}

bool ResonanceServer::_uses_parametric_or_hybrid() const {
    return reflection_type == 1 || reflection_type == 2;
}

bool ResonanceServer::_is_reflection_type_compatible(int baked_type) const {
    return (baked_type == 2) ||
        (baked_type == 0 && _uses_convolution_or_hybrid_or_tan()) ||
        (baked_type == 1 && _uses_parametric_or_hybrid());
}

bool ResonanceServer::_is_batch_compatible_with_config(int32_t handle) const {
    return probe_batch_registry_.is_compatible(handle, reflection_type, pathing_enabled);
}

int ResonanceServer::revalidate_probe_batches_with_config() {
    int n = probe_batch_registry_.revalidate_with_config(simulator, &simulation_mutex, reflection_type, pathing_enabled);
    if (n > 0) {
        std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
        reverb_param_cache_.clear();
    }
    return n;
}

void ResonanceServer::clear_probe_batches() {
    if (is_shutting_down_flag || !_ctx()) return;
    probe_batch_registry_.clear_batches(simulator, &simulation_mutex);
    std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
    reverb_param_cache_.clear();
}

// --- SETTERS ---
void ResonanceServer::set_debug_occlusion(bool p_enabled) { debug_occlusion = p_enabled; }
bool ResonanceServer::is_debug_occlusion_enabled() const { return debug_occlusion; }
void ResonanceServer::set_debug_reflections(bool p_enabled) { debug_reflections = p_enabled; }
bool ResonanceServer::is_debug_reflections_enabled() const { return debug_reflections; }
void ResonanceServer::set_debug_pathing(bool p_enabled) { debug_pathing = p_enabled; }
bool ResonanceServer::is_debug_pathing_enabled() const { return debug_pathing; }

Array ResonanceServer::get_pathing_visualization_segments() {
    Array result;
    std::lock_guard<std::mutex> lock(pathing_vis_mutex);
    for (const PathVisSegment& seg : pathing_vis_segments) {
        Dictionary d;
        d["from"] = seg.from;
        d["to"] = seg.to;
        d["occluded"] = seg.occluded;
        result.push_back(d);
    }
    return result;
}

Array ResonanceServer::get_ray_debug_segments() {
    Array result;
    if (!wants_debug_reflection_viz()) return result;
    IPLCoordinateSpace3 listener = get_current_listener_coords();
    IPLVector3 origin = listener.origin;
    ray_trace_debug_context_.trace_reflection_rays_for_viz(origin, max_rays, resonance::kRayDebugMaxDistance, result);
    return result;
}

Array ResonanceServer::get_ray_debug_segments_at(Vector3 origin) {
    Array result;
    if (!wants_debug_reflection_viz()) return result;
    IPLVector3 o;
    o.x = origin.x;
    o.y = origin.y;
    o.z = origin.z;
    ray_trace_debug_context_.trace_reflection_rays_for_viz(o, max_rays, resonance::kRayDebugMaxDistance, result);
    return result;
}

bool ResonanceServer::uses_custom_ray_tracer() const {
    return _scene_type() == IPL_SCENETYPE_CUSTOM;
}

int ResonanceServer::register_debug_mesh(const std::vector<IPLVector3>& vertices,
    const std::vector<IPLTriangle>& triangles,
    const IPLint32* material_indices, const IPLMatrix4x4* transform, const IPLMaterial* material) {
    return ray_trace_debug_context_.register_mesh(vertices, triangles, material_indices, transform, material);
}

Dictionary ResonanceServer::get_reverb_bus_instrumentation() const {
    Dictionary d;
    d["effect_process_calls"] = (int64_t)reverb_effect_process_calls.load(std::memory_order_relaxed);
    d["effect_mixer_null"] = (int64_t)reverb_effect_mixer_null.load(std::memory_order_relaxed);
    d["effect_success"] = (int64_t)reverb_effect_success.load(std::memory_order_relaxed);
    d["effect_frames_written"] = (int64_t)reverb_effect_frames_written.load(std::memory_order_relaxed);
    d["effect_output_peak"] = reverb_effect_output_peak.load(std::memory_order_relaxed);
    d["mixer_feed_count"] = (int64_t)reverb_mixer_feed_count.load(std::memory_order_relaxed);
    d["mixer_exists"] = (reflection_mixer != nullptr);
    d["reflection_type"] = reflection_type;
    d["convolution_valid_fetches"] = (int64_t)reverb_convolution_valid_fetches.load(std::memory_order_relaxed);
    d["convolution_feed_ir_null"] = (int64_t)reverb_convolution_feed_ir_null.load(std::memory_order_relaxed);
    d["convolution_gain_min"] = reverb_convolution_gain_min.load(std::memory_order_relaxed);
    d["convolution_gain_max"] = reverb_convolution_gain_max.load(std::memory_order_relaxed);
    d["convolution_input_rms_max"] = reverb_convolution_input_rms_max.load(std::memory_order_relaxed);
    return d;
}

void ResonanceServer::record_convolution_feed(bool ir_non_null, float reverb_gain, float input_rms) {
    if (!ir_non_null) reverb_convolution_feed_ir_null.fetch_add(1, std::memory_order_relaxed);
    float gmin = reverb_convolution_gain_min.load(std::memory_order_relaxed);
    if (reverb_gain < gmin) reverb_convolution_gain_min.store(reverb_gain, std::memory_order_relaxed);
    float gmax = reverb_convolution_gain_max.load(std::memory_order_relaxed);
    if (reverb_gain > gmax) reverb_convolution_gain_max.store(reverb_gain, std::memory_order_relaxed);
    float rmax = reverb_convolution_input_rms_max.load(std::memory_order_relaxed);
    if (input_rms > rmax) reverb_convolution_input_rms_max.store(input_rms, std::memory_order_relaxed);
}

void ResonanceServer::update_reverb_effect_instrumentation(bool mixer_null, bool success, int32_t frames_written, float output_peak) {
    reverb_effect_process_calls.fetch_add(1, std::memory_order_relaxed);
    if (mixer_null) reverb_effect_mixer_null.fetch_add(1, std::memory_order_relaxed);
    if (success) {
        reverb_effect_success.fetch_add(1, std::memory_order_relaxed);
        reverb_effect_frames_written.fetch_add(static_cast<uint64_t>(frames_written), std::memory_order_relaxed);
        float prev = reverb_effect_output_peak.load(std::memory_order_relaxed);
        if (output_peak > prev) reverb_effect_output_peak.store(output_peak, std::memory_order_relaxed);
    }
}

void ResonanceServer::unregister_debug_mesh(int mesh_id) {
    ray_trace_debug_context_.unregister_mesh(mesh_id);
}

void ResonanceServer::set_output_direct_enabled(bool p_enabled) { output_direct_enabled = p_enabled; }
bool ResonanceServer::is_output_direct_enabled() const { return output_direct_enabled; }
void ResonanceServer::set_output_reverb_enabled(bool p_enabled) { output_reverb_enabled = p_enabled; }
bool ResonanceServer::is_output_reverb_enabled() const { return output_reverb_enabled; }
void ResonanceServer::set_reverb_influence_radius(float p_radius) { reverb_influence_radius = std::max(1.0f, p_radius); }
float ResonanceServer::get_reverb_influence_radius() const { return reverb_influence_radius; }
void ResonanceServer::set_reverb_max_distance(float p_dist) { reverb_max_distance = std::max(0.0f, p_dist); }
float ResonanceServer::get_reverb_max_distance() const { return reverb_max_distance; }
void ResonanceServer::set_reverb_transmission_amount(float p_amount) { reverb_transmission_amount = std::max(0.0f, std::min(1.0f, p_amount)); }
float ResonanceServer::get_reverb_transmission_amount() const { return reverb_transmission_amount; }
void ResonanceServer::set_perspective_correction_enabled(bool p_enabled) { perspective_correction_enabled = p_enabled; }
bool ResonanceServer::is_perspective_correction_enabled() const { return perspective_correction_enabled; }
void ResonanceServer::set_perspective_correction_factor(float p_factor) { perspective_correction_factor = std::max(0.1f, std::min(3.0f, p_factor)); }
float ResonanceServer::get_perspective_correction_factor() const { return perspective_correction_factor; }

bool ResonanceServer::use_reverb_binaural() const {
    return reverb_binaural && _hrtf() != nullptr;
}

void ResonanceServer::emit_bake_progress(float progress) {
    emit_signal("bake_progress", progress);
}

void ResonanceServer::_bind_methods() {
    ADD_SIGNAL(MethodInfo("bake_progress", PropertyInfo(Variant::FLOAT, "progress")));
    ClassDB::bind_method(D_METHOD("init_audio_engine", "config"), &ResonanceServer::init_audio_engine);
    ClassDB::bind_method(D_METHOD("reinit_audio_engine", "config"), &ResonanceServer::reinit_audio_engine);

    ClassDB::bind_method(D_METHOD("get_version"), &ResonanceServer::get_version);
    ClassDB::bind_method(D_METHOD("is_initialized"), &ResonanceServer::is_initialized);
    ClassDB::bind_method(D_METHOD("is_simulating"), &ResonanceServer::is_simulating);
    ClassDB::bind_method(D_METHOD("get_sample_rate"), &ResonanceServer::get_sample_rate);
    ClassDB::bind_method(D_METHOD("get_audio_frame_size"), &ResonanceServer::get_audio_frame_size);
    ClassDB::bind_method(D_METHOD("update_listener", "pos", "dir", "up"), &ResonanceServer::update_listener);
    ClassDB::bind_method(D_METHOD("set_listener_valid", "valid"), &ResonanceServer::set_listener_valid);
    ClassDB::bind_method(D_METHOD("tick", "delta"), &ResonanceServer::tick);

    // Probes
    ClassDB::bind_method(D_METHOD("generate_manual_grid", "tr", "ext", "sp", "generation_type", "height_above_floor"),
        &ResonanceServer::generate_manual_grid, DEFVAL(2), DEFVAL(1.5));
    ClassDB::bind_method(D_METHOD("generate_probes_scene_aware", "volume_transform", "extents", "spacing", "generation_type", "height_above_floor"),
        &ResonanceServer::generate_probes_scene_aware, DEFVAL(1), DEFVAL(1.5));
    ClassDB::bind_method(D_METHOD("bake_manual_grid", "pts", "dat"), &ResonanceServer::bake_manual_grid);
    ClassDB::bind_method(D_METHOD("set_bake_params", "params"), &ResonanceServer::set_bake_params);
    ClassDB::bind_method(D_METHOD("set_bake_static_scene_asset", "p_asset"), &ResonanceServer::set_bake_static_scene_asset);
    ClassDB::bind_method(D_METHOD("load_static_scene_from_asset", "p_asset"), &ResonanceServer::load_static_scene_from_asset);
    ClassDB::bind_method(D_METHOD("add_static_scene_from_asset", "p_asset"), &ResonanceServer::add_static_scene_from_asset);
    ClassDB::bind_method(D_METHOD("clear_static_scenes"), &ResonanceServer::clear_static_scenes);
    ClassDB::bind_method(D_METHOD("set_bake_pipeline_pathing", "pathing"), &ResonanceServer::set_bake_pipeline_pathing);
    ClassDB::bind_method(D_METHOD("bake_probes_for_volume", "volume_transform", "extents", "spacing", "generation_type", "height_above_floor", "probe_data"),
        &ResonanceServer::bake_probes_for_volume);
    ClassDB::bind_method(D_METHOD("bake_pathing", "dat"), &ResonanceServer::bake_pathing);
    ClassDB::bind_method(D_METHOD("bake_static_source", "dat", "endpoint_position", "influence_radius"), &ResonanceServer::bake_static_source);
    ClassDB::bind_method(D_METHOD("bake_static_listener", "dat", "endpoint_position", "influence_radius"), &ResonanceServer::bake_static_listener);
    ClassDB::bind_method(D_METHOD("save_scene_obj", "file_base_name"), &ResonanceServer::save_scene_obj);
    ClassDB::bind_method(D_METHOD("export_static_scene_to_asset", "scene_root", "path"), &ResonanceServer::export_static_scene_to_asset);
    ClassDB::bind_method(D_METHOD("get_static_scene_hash", "scene_root"), &ResonanceServer::get_static_scene_hash);
    ClassDB::bind_method(D_METHOD("cancel_reflections_bake"), &ResonanceServer::cancel_reflections_bake);
    ClassDB::bind_method(D_METHOD("cancel_pathing_bake"), &ResonanceServer::cancel_pathing_bake);
    ClassDB::bind_method(D_METHOD("load_probe_batch", "dat"), &ResonanceServer::load_probe_batch);
    ClassDB::bind_method(D_METHOD("remove_probe_batch", "handle"), &ResonanceServer::remove_probe_batch);
    ClassDB::bind_method(D_METHOD("clear_probe_batches"), &ResonanceServer::clear_probe_batches);
    ClassDB::bind_method(D_METHOD("revalidate_probe_batches_with_config"), &ResonanceServer::revalidate_probe_batches_with_config);

    // Bind Setter/Getters
    ClassDB::bind_method(D_METHOD("set_debug_occlusion", "p_enabled"), &ResonanceServer::set_debug_occlusion);
    ClassDB::bind_method(D_METHOD("is_debug_occlusion_enabled"), &ResonanceServer::is_debug_occlusion_enabled);

    ClassDB::bind_method(D_METHOD("set_debug_reflections", "p_enabled"), &ResonanceServer::set_debug_reflections);
    ClassDB::bind_method(D_METHOD("is_debug_reflections_enabled"), &ResonanceServer::is_debug_reflections_enabled);
    ClassDB::bind_method(D_METHOD("set_debug_pathing", "p_enabled"), &ResonanceServer::set_debug_pathing);
    ClassDB::bind_method(D_METHOD("is_debug_pathing_enabled"), &ResonanceServer::is_debug_pathing_enabled);
    ClassDB::bind_method(D_METHOD("get_pathing_visualization_segments"), &ResonanceServer::get_pathing_visualization_segments);
    ClassDB::bind_method(D_METHOD("get_ray_debug_segments"), &ResonanceServer::get_ray_debug_segments);
    ClassDB::bind_method(D_METHOD("get_ray_debug_segments_at", "origin"), &ResonanceServer::get_ray_debug_segments_at);
    ClassDB::bind_method(D_METHOD("uses_custom_ray_tracer"), &ResonanceServer::uses_custom_ray_tracer);
    ClassDB::bind_method(D_METHOD("wants_debug_reflection_viz"), &ResonanceServer::wants_debug_reflection_viz);
    ClassDB::bind_method(D_METHOD("is_pathing_enabled"), &ResonanceServer::is_pathing_enabled);
    ClassDB::bind_method(D_METHOD("set_pathing_enabled", "p_enabled"), &ResonanceServer::set_pathing_enabled);

    ClassDB::bind_method(D_METHOD("set_output_direct_enabled", "p_enabled"), &ResonanceServer::set_output_direct_enabled);
    ClassDB::bind_method(D_METHOD("is_output_direct_enabled"), &ResonanceServer::is_output_direct_enabled);

    ClassDB::bind_method(D_METHOD("set_output_reverb_enabled", "p_enabled"), &ResonanceServer::set_output_reverb_enabled);
    ClassDB::bind_method(D_METHOD("is_output_reverb_enabled"), &ResonanceServer::is_output_reverb_enabled);

    ClassDB::bind_method(D_METHOD("set_reverb_influence_radius", "p_radius"), &ResonanceServer::set_reverb_influence_radius);
    ClassDB::bind_method(D_METHOD("get_reverb_influence_radius"), &ResonanceServer::get_reverb_influence_radius);
    ClassDB::bind_method(D_METHOD("set_reverb_max_distance", "p_dist"), &ResonanceServer::set_reverb_max_distance);
    ClassDB::bind_method(D_METHOD("get_reverb_max_distance"), &ResonanceServer::get_reverb_max_distance);
    ClassDB::bind_method(D_METHOD("set_reverb_transmission_amount", "p_amount"), &ResonanceServer::set_reverb_transmission_amount);
    ClassDB::bind_method(D_METHOD("get_reverb_transmission_amount"), &ResonanceServer::get_reverb_transmission_amount);
    ClassDB::bind_method(D_METHOD("set_perspective_correction_enabled", "p_enabled"), &ResonanceServer::set_perspective_correction_enabled);
    ClassDB::bind_method(D_METHOD("is_perspective_correction_enabled"), &ResonanceServer::is_perspective_correction_enabled);
    ClassDB::bind_method(D_METHOD("set_perspective_correction_factor", "p_factor"), &ResonanceServer::set_perspective_correction_factor);
    ClassDB::bind_method(D_METHOD("get_perspective_correction_factor"), &ResonanceServer::get_perspective_correction_factor);
    ClassDB::bind_method(D_METHOD("get_reflection_type"), &ResonanceServer::get_reflection_type);
    ClassDB::bind_method(D_METHOD("get_realtime_rays"), &ResonanceServer::get_realtime_rays);
    ClassDB::bind_method(D_METHOD("get_reverb_bus_instrumentation"), &ResonanceServer::get_reverb_bus_instrumentation);

    // Properties
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_occlusion"), "set_debug_occlusion", "is_debug_occlusion_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_reflections"), "set_debug_reflections", "is_debug_reflections_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_pathing"), "set_debug_pathing", "is_debug_pathing_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "output_direct"), "set_output_direct_enabled", "is_output_direct_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "output_reverb"), "set_output_reverb_enabled", "is_output_reverb_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_influence_radius", PROPERTY_HINT_RANGE, "1,50000,1"), "set_reverb_influence_radius", "get_reverb_influence_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_max_distance", PROPERTY_HINT_RANGE, "0,5000,1"), "set_reverb_max_distance", "get_reverb_max_distance");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_transmission_amount", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_reverb_transmission_amount", "get_reverb_transmission_amount");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "perspective_correction_enabled"), "set_perspective_correction_enabled", "is_perspective_correction_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "perspective_correction_factor", PROPERTY_HINT_RANGE, "0.5,2.0,0.1"), "set_perspective_correction_factor", "get_perspective_correction_factor");
}
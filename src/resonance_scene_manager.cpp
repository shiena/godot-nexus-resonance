#include "resonance_scene_manager.h"
#include "ray_trace_debug_context.h"
#include "resonance_constants.h"
#include "resonance_geometry.h"
#include "resonance_geometry_asset.h"
#include "resonance_ipl_guard.h"
#include "resonance_log.h"
#include "resonance_utils.h"
#include <cstring>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

void ResonanceSceneManager::collect_static_geometry_recursive(Node* node, std::vector<ResonanceGeometry*>& out) {
    if (!node)
        return;
    if (node->is_class("ResonanceGeometry")) {
        ResonanceGeometry* geom = Object::cast_to<ResonanceGeometry>(node);
        if (geom && !geom->is_dynamic()) {
            out.push_back(geom);
        }
    }
    for (int i = 0; i < node->get_child_count(); i++) {
        collect_static_geometry_recursive(node->get_child(i), out);
    }
}

/// Collects MeshInstance3D descendants, stopping at any ResonanceGeometry
static void collect_mesh_instances_from_children(Node* from, std::vector<MeshInstance3D*>& out) {
    if (!from)
        return;
    for (int i = 0; i < from->get_child_count(); i++) {
        Node* child = from->get_child(i);
        if (child->is_class("ResonanceGeometry"))
            continue; // Don't recurse; that geometry handles itself
        if (child->is_class("MeshInstance3D")) {
            MeshInstance3D* mi = Object::cast_to<MeshInstance3D>(child);
            if (mi && mi->is_visible_in_tree())
                out.push_back(mi);
        }
        collect_mesh_instances_from_children(child, out);
    }
}

void ResonanceSceneManager::collect_static_mesh_data(Node* scene_root, std::vector<IPLVector3>& out_vertices,
                                                     std::vector<IPLTriangle>& out_triangles, std::vector<IPLint32>* out_mat_indices) {
    out_vertices.clear();
    out_triangles.clear();
    if (out_mat_indices)
        out_mat_indices->clear();

    std::vector<ResonanceGeometry*> static_geoms;
    collect_static_geometry_recursive(scene_root, static_geoms);

    auto add_mesh_to_output = [&](const Ref<Mesh>& mesh, const Transform3D& xform) {
        if (mesh.is_null())
            return;
        for (int i = 0; i < mesh->get_surface_count(); i++) {
            Array arrays = mesh->surface_get_arrays(i);
            if (arrays.size() != Mesh::ARRAY_MAX)
                continue;

            PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
            PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];

            if (vertices.size() < 3)
                continue;

            size_t v_offset = out_vertices.size();

            for (int v = 0; v < vertices.size(); v++) {
                Vector3 vec = xform.xform(vertices[v]);
                out_vertices.push_back({vec.x, vec.y, vec.z});
            }

            if (!indices.is_empty()) {
                for (int idx = 0; idx < indices.size(); idx += 3) {
                    if (idx + 2 >= (int)indices.size())
                        break;
                    out_triangles.push_back({(int)indices[idx] + (int)v_offset,
                                             (int)indices[idx + 1] + (int)v_offset,
                                             (int)indices[idx + 2] + (int)v_offset});
                    if (out_mat_indices)
                        out_mat_indices->push_back(0);
                }
            } else {
                for (int v = 0; v < vertices.size(); v += 3) {
                    if (v + 2 >= vertices.size())
                        break;
                    out_triangles.push_back({(int)v + (int)v_offset,
                                             (int)v + 1 + (int)v_offset,
                                             (int)v + 2 + (int)v_offset});
                    if (out_mat_indices)
                        out_mat_indices->push_back(0);
                }
            }
        }
    };

    for (ResonanceGeometry* geom : static_geoms) {
        Node* parent = geom->get_parent();
        Node3D* node3d = Object::cast_to<Node3D>(parent);
        if (!node3d || !node3d->is_visible_in_tree())
            continue;

        MeshInstance3D* mesh_instance = Object::cast_to<MeshInstance3D>(parent);
        Ref<Mesh> mesh = geom->get_geometry_override();
        if (mesh.is_null() && mesh_instance)
            mesh = mesh_instance->get_mesh();
        // geometry_override mesh is in geom's local space; parent mesh is in parent's space.
        Transform3D xform = mesh == geom->get_geometry_override()
                                ? geom->get_global_transform()
                                : node3d->get_global_transform();
        add_mesh_to_output(mesh, xform);

        if (geom->get_export_all_children()) {
            std::vector<MeshInstance3D*> child_meshes;
            collect_mesh_instances_from_children(geom, child_meshes);
            for (MeshInstance3D* mi : child_meshes) {
                Ref<Mesh> m = mi->get_mesh();
                if (m.is_valid()) {
                    add_mesh_to_output(m, mi->get_global_transform());
                }
            }
        }
    }
}

bool ResonanceSceneManager::_build_temp_scene_for_export(std::vector<IPLVector3>& vertices,
                                                         std::vector<IPLTriangle>& triangles, std::vector<IPLint32>& mat_indices,
                                                         IPLContext* out_ctx, IPLScene* out_scene, IPLStaticMesh* out_mesh) {
    using namespace resonance;
    *out_ctx = nullptr;
    *out_scene = nullptr;
    *out_mesh = nullptr;

    IPLMaterial default_mat{};
    default_mat.absorption[0] = kSceneExportAbsorptionLow;
    default_mat.absorption[1] = kSceneExportAbsorptionMid;
    default_mat.absorption[2] = kSceneExportAbsorptionHigh;
    default_mat.scattering = kSceneExportScattering;
    default_mat.transmission[0] = default_mat.transmission[1] = default_mat.transmission[2] = kSceneExportTransmission;

    IPLStaticMeshSettings mesh_settings{};
    mesh_settings.materials = &default_mat;
    mesh_settings.numVertices = (IPLint32)vertices.size();
    mesh_settings.numTriangles = (IPLint32)triangles.size();
    mesh_settings.numMaterials = 1;
    mesh_settings.vertices = vertices.data();
    mesh_settings.triangles = triangles.data();
    mesh_settings.materialIndices = mat_indices.data();

    IPLContext export_context = nullptr;
    IPLContextSettings ctx_settings{};
    ctx_settings.version = STEAMAUDIO_VERSION;
    if (iplContextCreate(&ctx_settings, &export_context) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceSceneManager: iplContextCreate failed (_build_temp_scene_for_export).");
        return false;
    }
    IPLScopedRelease<IPLContext> ctxGuard(export_context, iplContextRelease);

    IPLScene temp_scene = nullptr;
    IPLSceneSettings scene_settings{};
    scene_settings.type = IPL_SCENETYPE_DEFAULT;
    if (iplSceneCreate(export_context, &scene_settings, &temp_scene) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceSceneManager: iplSceneCreate failed (_build_temp_scene_for_export).");
        return false;
    }
    IPLScopedRelease<IPLScene> sceneGuard(temp_scene, iplSceneRelease);

    IPLStaticMesh temp_mesh = nullptr;
    if (iplStaticMeshCreate(temp_scene, &mesh_settings, &temp_mesh) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceSceneManager: iplStaticMeshCreate failed (_build_temp_scene_for_export).");
        return false;
    }
    IPLScopedRelease<IPLStaticMesh> meshGuard(temp_mesh, iplStaticMeshRelease);

    iplStaticMeshAdd(temp_mesh, temp_scene);
    iplSceneCommit(temp_scene);

    *out_ctx = export_context;
    *out_scene = temp_scene;
    *out_mesh = temp_mesh;
    ctxGuard.detach();
    sceneGuard.detach();
    meshGuard.detach();
    return true;
}

int ResonanceSceneManager::register_asset_debug_geometry(const Ref<ResonanceGeometryAsset>& asset, RayTraceDebugContext* debug_ctx,
                                                         const Transform3D& transform) {
    if (!asset.is_valid() || !asset->has_debug_geometry() || !debug_ctx)
        return -1;
    PackedVector3Array pv = asset->get_debug_vertices();
    PackedInt32Array pt = asset->get_debug_triangles();
    std::vector<IPLVector3> ipl_verts;
    ipl_verts.resize(pv.size());
    for (int i = 0; i < pv.size(); i++) {
        Vector3 v = pv[i];
        ipl_verts[i] = {v.x, v.y, v.z};
    }
    std::vector<IPLTriangle> ipl_tris;
    int num_tris = pt.size() / 3;
    ipl_tris.resize(num_tris);
    std::vector<IPLint32> ipl_mat_indices(num_tris, 0);
    for (int i = 0; i < num_tris && (i * 3 + 2) < pt.size(); i++) {
        ipl_tris[i].indices[0] = pt[i * 3 + 0];
        ipl_tris[i].indices[1] = pt[i * 3 + 1];
        ipl_tris[i].indices[2] = pt[i * 3 + 2];
    }
    using namespace resonance;
    IPLMaterial mat{};
    // Use uniform low absorption for debug visualization.
    mat.absorption[0] = mat.absorption[1] = mat.absorption[2] = kSceneExportAbsorptionLow;
    mat.scattering = kSceneExportScattering;
    mat.transmission[0] = mat.transmission[1] = mat.transmission[2] = kSceneExportTransmission;
    IPLMatrix4x4 ipl_xform = ResonanceUtils::to_ipl_matrix(transform);
    return debug_ctx->register_mesh(ipl_verts, ipl_tris, ipl_mat_indices.data(), &ipl_xform, &mat);
}

void ResonanceSceneManager::save_scene_data(IPLContext ctx, IPLScene scene, const String& filename) {
    if (!ctx) {
        ResonanceLog::error("ResonanceSceneManager: Context is null (save_scene_data).");
        return;
    }
    if (!scene) {
        UtilityFunctions::push_warning("Nexus Resonance: No scene to save.");
        return;
    }
    IPLSerializedObjectSettings serialSettings{};
    IPLSerializedObject serializedObject = nullptr;
    if (iplSerializedObjectCreate(ctx, &serialSettings, &serializedObject) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceSceneManager: iplSerializedObjectCreate failed (save_scene_data).");
        return;
    }
    iplSceneSave(scene, serializedObject);

    IPLsize size = iplSerializedObjectGetSize(serializedObject);
    IPLbyte* data = iplSerializedObjectGetData(serializedObject);
    if (size == 0 || !data) {
        UtilityFunctions::push_error("Nexus Resonance: Scene save produced no data. Possible causes: empty scene or no geometry committed. Check Steam Audio log output for details.");
        iplSerializedObjectRelease(&serializedObject);
        return;
    }

    Ref<FileAccess> file = FileAccess::open(filename, FileAccess::WRITE);
    if (file.is_valid()) {
        PackedByteArray pba;
        pba.resize((int64_t)size);
        memcpy(pba.ptrw(), data, size);
        file->store_buffer(pba);
        file->close();
        UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] Scene saved successfully to " + filename);
    } else {
        UtilityFunctions::push_error("Nexus Resonance: Failed to open file for writing: ", filename);
    }
    iplSerializedObjectRelease(&serializedObject);
}

void ResonanceSceneManager::load_scene_data(IPLContext ctx, IPLScene* out_scene, IPLSimulator sim,
                                            IPLSceneType scene_type, IPLEmbreeDevice embree, IPLRadeonRaysDevice radeon,
                                            const String& filename, int* out_global_triangle_count) {
    if (!ctx) {
        ResonanceLog::error("ResonanceSceneManager: Context is null (load_scene_data).");
        return;
    }
    if (!FileAccess::file_exists(filename)) {
        UtilityFunctions::push_error("Nexus Resonance: File not found: ", filename);
        return;
    }
    Ref<FileAccess> file = FileAccess::open(filename, FileAccess::READ);
    if (file.is_null()) {
        ResonanceLog::error("ResonanceSceneManager: Failed to open file for reading: " + filename);
        return;
    }

    PackedByteArray pba = file->get_buffer(file->get_length());
    file->close();

    if (pba.is_empty()) {
        UtilityFunctions::push_error("Nexus Resonance: Scene file is empty.");
        return;
    }

    if (*out_scene) {
        iplSceneRelease(out_scene);
        *out_scene = nullptr;
    }

    IPLSerializedObjectSettings serialSettings{};
    serialSettings.data = (IPLbyte*)pba.ptr();
    serialSettings.size = pba.size();

    IPLSerializedObject serializedObject = nullptr;
    if (iplSerializedObjectCreate(ctx, &serialSettings, &serializedObject) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceSceneManager: iplSerializedObjectCreate failed (load_scene_data).");
        return;
    }

    IPLSceneSettings sceneSettings{};
    sceneSettings.type = scene_type;
    sceneSettings.embreeDevice = embree;
    sceneSettings.radeonRaysDevice = radeon;

    IPLerror status = iplSceneLoad(ctx, &sceneSettings, serializedObject, nullptr, nullptr, out_scene);
    iplSerializedObjectRelease(&serializedObject);

    if (status == IPL_STATUS_SUCCESS) {
        iplSimulatorSetScene(sim, *out_scene);
        iplSimulatorCommit(sim);
        // IPL API does not expose triangle count when loading from serialized file. Use 1 as "scene loaded" marker for is_simulating() (global_triangle_count > 0).
        if (out_global_triangle_count)
            *out_global_triangle_count = 1;
        UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] Scene loaded successfully from " + filename);
    } else {
        UtilityFunctions::push_error("Nexus Resonance: Failed to load scene.");
        *out_scene = nullptr; // iplSceneLoad failed; ensure clean state before fallback
        IPLerror fallback_status = iplSceneCreate(ctx, &sceneSettings, out_scene);
        if (fallback_status != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceSceneManager: iplSceneCreate failed (load_scene_data fallback).");
            return;
        }
        iplSimulatorSetScene(sim, *out_scene);
        iplSimulatorCommit(sim);
        if (out_global_triangle_count)
            *out_global_triangle_count = 0;
    }
}

void ResonanceSceneManager::add_static_scene_from_asset(IPLContext ctx, IPLScene scene, const Ref<ResonanceGeometryAsset>& asset,
                                                        RayTraceDebugContext* debug_ctx, bool wants_debug_viz, RuntimeSceneState& state,
                                                        const Transform3D& transform, IPLSceneType scene_type, IPLEmbreeDevice embree, IPLRadeonRaysDevice radeon) {
    if (!asset.is_valid() || !asset->is_valid() || !scene || asset->get_size() == 0)
        return;

    IPLSerializedObjectSettings serialSettings{};
    serialSettings.data = const_cast<IPLbyte*>(reinterpret_cast<const IPLbyte*>(asset->get_data_ptr()));
    serialSettings.size = static_cast<IPLsize>(asset->get_size());
    IPLSerializedObject serialObj = nullptr;
    if (iplSerializedObjectCreate(ctx, &serialSettings, &serialObj) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceSceneManager: iplSerializedObjectCreate failed (add_static_scene_from_asset).");
        return;
    }

    const bool use_instanced = !transform.is_equal_approx(Transform3D());
    int tri = asset->get_triangle_count();

    if (use_instanced) {
        IPLSceneSettings subSceneSettings{};
        subSceneSettings.type = scene_type;
        subSceneSettings.embreeDevice = embree;
        subSceneSettings.radeonRaysDevice = radeon;
        IPLScene sub_scene = nullptr;
        if (iplSceneCreate(ctx, &subSceneSettings, &sub_scene) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceSceneManager: iplSceneCreate failed (add_static_scene_from_asset instanced).");
            iplSerializedObjectRelease(&serialObj);
            return;
        }

        IPLStaticMesh loadMesh = nullptr;
        if (iplStaticMeshLoad(sub_scene, serialObj, nullptr, nullptr, &loadMesh) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceSceneManager: iplStaticMeshLoad failed (add_static_scene_from_asset instanced).");
            iplSerializedObjectRelease(&serialObj);
            iplSceneRelease(&sub_scene);
            return;
        }
        iplSerializedObjectRelease(&serialObj);
        iplStaticMeshAdd(loadMesh, sub_scene);
        iplSceneCommit(sub_scene);

        IPLInstancedMeshSettings instSettings{};
        instSettings.subScene = sub_scene;
        instSettings.transform = ResonanceUtils::to_ipl_matrix(transform);

        IPLInstancedMesh inst_mesh = nullptr;
        if (iplInstancedMeshCreate(scene, &instSettings, &inst_mesh) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceSceneManager: iplInstancedMeshCreate failed (add_static_scene_from_asset).");
            iplStaticMeshRemove(loadMesh, sub_scene);
            iplStaticMeshRelease(&loadMesh);
            iplSceneRelease(&sub_scene);
            return;
        }
        iplInstancedMeshAdd(inst_mesh, scene);
        iplSceneCommit(scene);

        state.sub_scenes.push_back(sub_scene);
        state.instanced_meshes.push_back(inst_mesh);
    } else {
        IPLStaticMesh loadMesh = nullptr;
        if (iplStaticMeshLoad(scene, serialObj, nullptr, nullptr, &loadMesh) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceSceneManager: iplStaticMeshLoad failed (add_static_scene_from_asset direct).");
            iplSerializedObjectRelease(&serialObj);
            return;
        }
        iplSerializedObjectRelease(&serialObj);

        iplStaticMeshAdd(loadMesh, scene);
        iplSceneCommit(scene);
        state.meshes.push_back(loadMesh);
    }

    state.tri_count += tri;
    if (tri > 0 && state.global_triangle_count)
        *state.global_triangle_count += tri;
    if (state.scene_dirty)
        state.scene_dirty->store(true);
    if (wants_debug_viz && debug_ctx) {
        int dbg_id = register_asset_debug_geometry(asset, debug_ctx, transform);
        if (dbg_id >= 0)
            state.debug_ids.push_back(dbg_id);
    }
}

void ResonanceSceneManager::load_static_scene_from_asset(IPLContext ctx, IPLScene scene, const Ref<ResonanceGeometryAsset>& asset,
                                                         RayTraceDebugContext* debug_ctx, bool wants_debug_viz, RuntimeSceneState& state,
                                                         const Transform3D& transform, IPLSceneType scene_type, IPLEmbreeDevice embree, IPLRadeonRaysDevice radeon) {
    if (!scene)
        return;
    clear_static_scenes(scene, debug_ctx, state);

    if (!asset.is_valid() || !asset->is_valid())
        return;
    add_static_scene_from_asset(ctx, scene, asset, debug_ctx, wants_debug_viz, state, transform, scene_type, embree, radeon);
}

void ResonanceSceneManager::clear_static_scenes(IPLScene scene, RayTraceDebugContext* debug_ctx, RuntimeSceneState& state) {
    if (!scene)
        return;
    for (int id : state.debug_ids) {
        if (debug_ctx)
            debug_ctx->unregister_mesh(id);
    }
    state.debug_ids.clear();
    for (IPLStaticMesh m : state.meshes) {
        if (m) {
            iplStaticMeshRemove(m, scene);
            iplStaticMeshRelease(&m);
        }
    }
    state.meshes.clear();
    for (IPLInstancedMesh& im : state.instanced_meshes) {
        if (im) {
            iplInstancedMeshRemove(im, scene);
            iplInstancedMeshRelease(&im);
        }
    }
    state.instanced_meshes.clear();
    for (IPLScene& sub : state.sub_scenes) {
        if (sub)
            iplSceneRelease(&sub);
    }
    state.sub_scenes.clear();
    if (state.tri_count > 0 && state.global_triangle_count)
        *state.global_triangle_count -= state.tri_count;
    state.tri_count = 0;
    if (state.scene_dirty)
        state.scene_dirty->store(true);
}

Error ResonanceSceneManager::export_static_scene_to_asset(Node* scene_root, const String& p_path) {
    if (!scene_root)
        return ERR_INVALID_PARAMETER;

    std::vector<IPLVector3> ipl_vertices;
    std::vector<IPLTriangle> ipl_triangles;
    std::vector<IPLint32> ipl_mat_indices;
    collect_static_mesh_data(scene_root, ipl_vertices, ipl_triangles, &ipl_mat_indices);

    if (ipl_triangles.empty()) {
        UtilityFunctions::push_warning("Nexus Resonance: No valid mesh data in static geometry.");
        return ERR_INVALID_PARAMETER;
    }

    IPLContext export_context = nullptr;
    IPLScene temp_scene = nullptr;
    IPLStaticMesh temp_mesh = nullptr;
    if (!_build_temp_scene_for_export(ipl_vertices, ipl_triangles, ipl_mat_indices, &export_context, &temp_scene, &temp_mesh)) {
        return ERR_CANT_CREATE;
    }

    IPLSerializedObjectSettings serial_settings{};
    IPLSerializedObject serial_obj = nullptr;
    if (iplSerializedObjectCreate(export_context, &serial_settings, &serial_obj) != IPL_STATUS_SUCCESS) {
        iplStaticMeshRelease(&temp_mesh);
        iplSceneRelease(&temp_scene);
        iplContextRelease(&export_context);
        return ERR_CANT_CREATE;
    }

    iplStaticMeshSave(temp_mesh, serial_obj);

    IPLsize size = iplSerializedObjectGetSize(serial_obj);
    IPLbyte* data = iplSerializedObjectGetData(serial_obj);
    if (!data || size == 0) {
        iplSerializedObjectRelease(&serial_obj);
        iplStaticMeshRelease(&temp_mesh);
        iplSceneRelease(&temp_scene);
        iplContextRelease(&export_context);
        return ERR_CANT_CREATE;
    }

    Ref<ResonanceGeometryAsset> asset;
    asset.instantiate();
    PackedByteArray pba;
    pba.resize((int64_t)size);
    memcpy(pba.ptrw(), data, size);
    asset->set_mesh_data(pba);
    asset->set_triangle_count((int)ipl_triangles.size());
    PackedVector3Array dbg_verts;
    dbg_verts.resize((int)ipl_vertices.size());
    for (size_t i = 0; i < ipl_vertices.size(); i++) {
        dbg_verts.set((int)i, Vector3(ipl_vertices[i].x, ipl_vertices[i].y, ipl_vertices[i].z));
    }
    PackedInt32Array dbg_tris;
    dbg_tris.resize((int)(ipl_triangles.size() * 3));
    for (size_t i = 0; i < ipl_triangles.size(); i++) {
        dbg_tris.set((int)(i * 3 + 0), ipl_triangles[i].indices[0]);
        dbg_tris.set((int)(i * 3 + 1), ipl_triangles[i].indices[1]);
        dbg_tris.set((int)(i * 3 + 2), ipl_triangles[i].indices[2]);
    }
    asset->set_debug_vertices(dbg_verts);
    asset->set_debug_triangles(dbg_tris);

    iplSerializedObjectRelease(&serial_obj);
    iplStaticMeshRelease(&temp_mesh);
    iplSceneRelease(&temp_scene);
    iplContextRelease(&export_context);

    String path = p_path;
    if (!path.ends_with(".tres") && !path.ends_with(".res")) {
        path += ".tres";
    }
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps && (path.begins_with("res://") || path.begins_with("user://"))) {
        path = ps->globalize_path(path);
    }
    // Use .tres temp file so ResourceSaver recognizes the format (ERR_FILE_UNRECOGNIZED with .tmp)
    int64_t ext_pos = path.rfind(".");
    String tmp_path = (ext_pos >= 0) ? (path.substr(0, ext_pos) + "_tmp.tres") : (path + "_tmp.tres");
    Error save_err = ResourceSaver::get_singleton()->save(asset, tmp_path, ResourceSaver::FLAG_CHANGE_PATH);
    if (save_err != OK) {
        return save_err;
    }
    Error rename_err = DirAccess::rename_absolute(tmp_path, path);
    if (rename_err != OK) {
        DirAccess::remove_absolute(tmp_path);
        return rename_err;
    }
    if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] Static scene exported to " + path + " (" + String::num((int)ipl_triangles.size()) + " triangles).");
    }
    return OK;
}

Error ResonanceSceneManager::export_static_scene_to_obj(Node* scene_root, const String& file_base_name) {
    if (!scene_root)
        return ERR_INVALID_PARAMETER;

    std::vector<IPLVector3> ipl_vertices;
    std::vector<IPLTriangle> ipl_triangles;
    std::vector<IPLint32> ipl_mat_indices;
    collect_static_mesh_data(scene_root, ipl_vertices, ipl_triangles, &ipl_mat_indices);

    if (ipl_triangles.empty()) {
        UtilityFunctions::push_warning("Nexus Resonance: No valid mesh data in static geometry.");
        return ERR_INVALID_PARAMETER;
    }

    // Flip winding for OBJ export so Godot importer shows correct normals
    for (auto& tri : ipl_triangles) {
        std::swap(tri.indices[1], tri.indices[2]);
    }

    IPLContext export_context = nullptr;
    IPLScene temp_scene = nullptr;
    IPLStaticMesh temp_mesh = nullptr;
    if (!_build_temp_scene_for_export(ipl_vertices, ipl_triangles, ipl_mat_indices, &export_context, &temp_scene, &temp_mesh)) {
        return ERR_CANT_CREATE;
    }

    String path = file_base_name;
    if (!path.ends_with(".obj")) {
        path = path + ".obj";
    }
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps && (path.begins_with("res://") || path.begins_with("user://"))) {
        path = ps->globalize_path(path);
    }
    CharString cs = path.utf8();

    iplSceneSaveOBJ(temp_scene, cs.get_data());

    iplStaticMeshRelease(&temp_mesh);
    iplSceneRelease(&temp_scene);
    iplContextRelease(&export_context);

    if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] Scene exported to OBJ: " + path + " (" + String::num((int)ipl_triangles.size()) + " triangles).");
    }
    return OK;
}

int64_t ResonanceSceneManager::get_static_scene_hash(Node* scene_root, std::function<uint64_t(const PackedByteArray&)> hash_fn) {
    if (!scene_root)
        return 0;
    std::vector<IPLVector3> ipl_vertices;
    std::vector<IPLTriangle> ipl_triangles;
    collect_static_mesh_data(scene_root, ipl_vertices, ipl_triangles, nullptr);
    if (ipl_triangles.empty())
        return 0;

    PackedByteArray pba;
    pba.resize((int)(ipl_vertices.size() * sizeof(IPLVector3) + ipl_triangles.size() * sizeof(IPLTriangle)));
    uint8_t* w = pba.ptrw();
    memcpy(w, ipl_vertices.data(), ipl_vertices.size() * sizeof(IPLVector3));
    memcpy(w + ipl_vertices.size() * sizeof(IPLVector3), ipl_triangles.data(), ipl_triangles.size() * sizeof(IPLTriangle));
    return (int64_t)hash_fn(pba);
}

} // namespace godot

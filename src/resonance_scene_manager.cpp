#include "resonance_scene_manager.h"
#include "resonance_geometry.h"
#include "resonance_geometry_asset.h"
#include "ray_trace_debug_context.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>

namespace godot {

void ResonanceSceneManager::collect_static_geometry_recursive(Node* node, std::vector<ResonanceGeometry*>& out) {
    if (!node) return;
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

void ResonanceSceneManager::collect_static_mesh_data(Node* scene_root, std::vector<IPLVector3>& out_vertices,
    std::vector<IPLTriangle>& out_triangles, std::vector<IPLint32>* out_mat_indices) {
    out_vertices.clear();
    out_triangles.clear();
    if (out_mat_indices) out_mat_indices->clear();

    std::vector<ResonanceGeometry*> static_geoms;
    collect_static_geometry_recursive(scene_root, static_geoms);

    for (ResonanceGeometry* geom : static_geoms) {
        Node* parent = geom->get_parent();
        Node3D* node3d = Object::cast_to<Node3D>(parent);
        if (!node3d || !node3d->is_visible_in_tree()) continue;

        MeshInstance3D* mesh_instance = Object::cast_to<MeshInstance3D>(parent);
        Ref<Mesh> mesh = geom->get_geometry_override();
        if (mesh.is_null() && mesh_instance) mesh = mesh_instance->get_mesh();
        if (mesh.is_null()) continue;

        Transform3D xform = node3d->get_global_transform();

        for (int i = 0; i < mesh->get_surface_count(); i++) {
            Array arrays = mesh->surface_get_arrays(i);
            if (arrays.size() != Mesh::ARRAY_MAX) continue;

            PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
            PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];

            if (vertices.size() < 3) continue;

            size_t v_offset = out_vertices.size();

            for (int v = 0; v < vertices.size(); v++) {
                Vector3 vec = xform.xform(vertices[v]);
                out_vertices.push_back({ vec.x, vec.y, vec.z });
            }

            if (indices.size() > 0) {
                for (int idx = 0; idx < indices.size(); idx += 3) {
                    if (idx + 2 >= (int)indices.size()) break;
                    out_triangles.push_back({
                        (int)indices[idx] + (int)v_offset,
                        (int)indices[idx + 1] + (int)v_offset,
                        (int)indices[idx + 2] + (int)v_offset
                    });
                    if (out_mat_indices) out_mat_indices->push_back(0);
                }
            } else {
                for (int v = 0; v < vertices.size(); v += 3) {
                    if (v + 2 >= vertices.size()) break;
                    out_triangles.push_back({
                        (int)v + (int)v_offset,
                        (int)v + 1 + (int)v_offset,
                        (int)v + 2 + (int)v_offset
                    });
                    if (out_mat_indices) out_mat_indices->push_back(0);
                }
            }
        }
    }
}

int ResonanceSceneManager::register_asset_debug_geometry(const Ref<ResonanceGeometryAsset>& asset, RayTraceDebugContext* debug_ctx) {
    if (!asset.is_valid() || !asset->has_debug_geometry() || !debug_ctx) return -1;
    PackedVector3Array pv = asset->get_debug_vertices();
    PackedInt32Array pt = asset->get_debug_triangles();
    std::vector<IPLVector3> ipl_verts;
    ipl_verts.resize(pv.size());
    for (int i = 0; i < pv.size(); i++) {
        Vector3 v = pv[i];
        ipl_verts[i] = { v.x, v.y, v.z };
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
    IPLMaterial mat{};
    mat.absorption[0] = mat.absorption[1] = mat.absorption[2] = 0.1f;
    mat.scattering = 0.5f;
    mat.transmission[0] = mat.transmission[1] = mat.transmission[2] = 0.1f;
    IPLMatrix4x4 ident{};
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) ident.elements[r][c] = (r == c) ? 1.0f : 0.0f;
    return debug_ctx->register_mesh(ipl_verts, ipl_tris, ipl_mat_indices.data(), &ident, &mat);
}

void ResonanceSceneManager::save_scene_data(IPLContext ctx, IPLScene scene, const String& filename) {
    if (!scene) {
        UtilityFunctions::push_warning("Nexus Resonance: No scene to save.");
        return;
    }
    IPLSerializedObjectSettings serialSettings{};
    IPLSerializedObject serializedObject = nullptr;
    iplSerializedObjectCreate(ctx, &serialSettings, &serializedObject);
    iplSceneSave(scene, serializedObject);

    IPLsize size = iplSerializedObjectGetSize(serializedObject);
    IPLbyte* data = iplSerializedObjectGetData(serializedObject);

    Ref<FileAccess> file = FileAccess::open(filename, FileAccess::WRITE);
    if (file.is_valid()) {
        PackedByteArray pba;
        pba.resize((int64_t)size);
        memcpy(pba.ptrw(), data, size);
        file->store_buffer(pba);
        file->close();
        UtilityFunctions::print("Nexus Resonance: Scene saved successfully to ", filename);
    } else {
        UtilityFunctions::push_error("Nexus Resonance: Failed to open file for writing: ", filename);
    }
    iplSerializedObjectRelease(&serializedObject);
}

void ResonanceSceneManager::load_scene_data(IPLContext ctx, IPLScene* out_scene, IPLSimulator sim,
    IPLSceneType scene_type, IPLEmbreeDevice embree, IPLRadeonRaysDevice radeon,
    const String& filename, int* out_global_triangle_count) {
    if (!FileAccess::file_exists(filename)) {
        UtilityFunctions::push_error("Nexus Resonance: File not found: ", filename);
        return;
    }
    Ref<FileAccess> file = FileAccess::open(filename, FileAccess::READ);
    if (file.is_null()) return;

    PackedByteArray pba = file->get_buffer(file->get_length());
    file->close();

    if (*out_scene) {
        iplSceneRelease(out_scene);
        *out_scene = nullptr;
    }

    IPLSerializedObjectSettings serialSettings{};
    serialSettings.data = (IPLbyte*)pba.ptr();
    serialSettings.size = pba.size();

    IPLSerializedObject serializedObject = nullptr;
    iplSerializedObjectCreate(ctx, &serialSettings, &serializedObject);

    IPLSceneSettings sceneSettings{};
    sceneSettings.type = scene_type;
    sceneSettings.embreeDevice = embree;
    sceneSettings.radeonRaysDevice = radeon;

    IPLerror status = iplSceneLoad(ctx, &sceneSettings, serializedObject, nullptr, nullptr, out_scene);
    iplSerializedObjectRelease(&serializedObject);

    if (status == IPL_STATUS_SUCCESS) {
        iplSimulatorSetScene(sim, *out_scene);
        iplSimulatorCommit(sim);
        if (out_global_triangle_count) *out_global_triangle_count = 1;
        UtilityFunctions::print("Nexus Resonance: Scene loaded successfully from ", filename);
    } else {
        UtilityFunctions::push_error("Nexus Resonance: Failed to load scene.");
        iplSceneCreate(ctx, &sceneSettings, out_scene);
        iplSimulatorSetScene(sim, *out_scene);
        iplSimulatorCommit(sim);
        if (out_global_triangle_count) *out_global_triangle_count = 0;
    }
}

void ResonanceSceneManager::add_static_scene_from_asset(IPLContext ctx, IPLScene scene, const Ref<ResonanceGeometryAsset>& asset,
    RayTraceDebugContext* debug_ctx, bool wants_debug_viz,
    std::vector<IPLStaticMesh>& runtime_meshes, int& runtime_tri_count,
    std::vector<int>& runtime_debug_ids, int* global_triangle_count, std::atomic<bool>* scene_dirty) {
    if (!asset.is_valid() || !asset->is_valid() || !scene) return;

    IPLSerializedObjectSettings serialSettings{};
    serialSettings.data = const_cast<IPLbyte*>(reinterpret_cast<const IPLbyte*>(asset->get_data_ptr()));
    serialSettings.size = static_cast<IPLsize>(asset->get_size());
    IPLSerializedObject serialObj = nullptr;
    if (iplSerializedObjectCreate(ctx, &serialSettings, &serialObj) != IPL_STATUS_SUCCESS) return;

    IPLStaticMesh loadMesh = nullptr;
    if (iplStaticMeshLoad(scene, serialObj, nullptr, nullptr, &loadMesh) != IPL_STATUS_SUCCESS) {
        iplSerializedObjectRelease(&serialObj);
        return;
    }
    iplSerializedObjectRelease(&serialObj);

    iplStaticMeshAdd(loadMesh, scene);
    iplSceneCommit(scene);
    runtime_meshes.push_back(loadMesh);
    int tri = asset->get_triangle_count();
    runtime_tri_count += tri;
    if (tri > 0 && global_triangle_count) *global_triangle_count += tri;
    if (scene_dirty) scene_dirty->store(true);
    if (wants_debug_viz && debug_ctx) {
        int dbg_id = register_asset_debug_geometry(asset, debug_ctx);
        if (dbg_id >= 0) runtime_debug_ids.push_back(dbg_id);
    }
}

void ResonanceSceneManager::load_static_scene_from_asset(IPLContext ctx, IPLScene scene, const Ref<ResonanceGeometryAsset>& asset,
    RayTraceDebugContext* debug_ctx, bool wants_debug_viz,
    std::vector<IPLStaticMesh>& runtime_meshes, int& runtime_tri_count,
    std::vector<int>& runtime_debug_ids, int* global_triangle_count, std::atomic<bool>* scene_dirty) {
    if (!scene) return;
    for (int id : runtime_debug_ids) {
        if (debug_ctx) debug_ctx->unregister_mesh(id);
    }
    runtime_debug_ids.clear();
    for (IPLStaticMesh m : runtime_meshes) {
        if (m) {
            iplStaticMeshRemove(m, scene);
            iplStaticMeshRelease(&m);
        }
    }
    runtime_meshes.clear();
    if (runtime_tri_count > 0 && global_triangle_count) *global_triangle_count -= runtime_tri_count;
    runtime_tri_count = 0;
    if (scene_dirty) scene_dirty->store(true);

    if (!asset.is_valid() || !asset->is_valid()) return;
    add_static_scene_from_asset(ctx, scene, asset, debug_ctx, wants_debug_viz,
        runtime_meshes, runtime_tri_count, runtime_debug_ids, global_triangle_count, scene_dirty);
}

void ResonanceSceneManager::clear_static_scenes(IPLScene scene, RayTraceDebugContext* debug_ctx,
    std::vector<IPLStaticMesh>& runtime_meshes, int& runtime_tri_count,
    std::vector<int>& runtime_debug_ids, int* global_triangle_count, std::atomic<bool>* scene_dirty) {
    if (!scene) return;
    for (int id : runtime_debug_ids) {
        if (debug_ctx) debug_ctx->unregister_mesh(id);
    }
    runtime_debug_ids.clear();
    for (IPLStaticMesh m : runtime_meshes) {
        if (m) {
            iplStaticMeshRemove(m, scene);
            iplStaticMeshRelease(&m);
        }
    }
    runtime_meshes.clear();
    if (runtime_tri_count > 0 && global_triangle_count) *global_triangle_count -= runtime_tri_count;
    runtime_tri_count = 0;
    if (scene_dirty) scene_dirty->store(true);
}

Error ResonanceSceneManager::export_static_scene_to_asset(Node* scene_root, const String& p_path) {
    if (!scene_root) return ERR_INVALID_PARAMETER;

    std::vector<IPLVector3> ipl_vertices;
    std::vector<IPLTriangle> ipl_triangles;
    std::vector<IPLint32> ipl_mat_indices;
    collect_static_mesh_data(scene_root, ipl_vertices, ipl_triangles, &ipl_mat_indices);

    if (ipl_triangles.empty()) {
        UtilityFunctions::push_warning("Nexus Resonance: No valid mesh data in static geometry.");
        return ERR_INVALID_PARAMETER;
    }

    IPLMaterial default_mat{};
    default_mat.absorption[0] = 0.1f;
    default_mat.absorption[1] = 0.2f;
    default_mat.absorption[2] = 0.1f;
    default_mat.scattering = 0.5f;
    default_mat.transmission[0] = default_mat.transmission[1] = default_mat.transmission[2] = 0.1f;
    IPLStaticMeshSettings mesh_settings{};
    mesh_settings.materials = &default_mat;
    mesh_settings.numVertices = (IPLint32)ipl_vertices.size();
    mesh_settings.numTriangles = (IPLint32)ipl_triangles.size();
    mesh_settings.numMaterials = 1;
    mesh_settings.vertices = ipl_vertices.data();
    mesh_settings.triangles = ipl_triangles.data();
    mesh_settings.materialIndices = ipl_mat_indices.data();

    IPLContext export_context = nullptr;
    IPLContextSettings ctx_settings{};
    ctx_settings.version = STEAMAUDIO_VERSION;
    if (iplContextCreate(&ctx_settings, &export_context) != IPL_STATUS_SUCCESS) {
        return ERR_CANT_CREATE;
    }

    IPLSceneSettings scene_settings{};
    scene_settings.type = IPL_SCENETYPE_DEFAULT;

    IPLScene temp_scene = nullptr;
    if (iplSceneCreate(export_context, &scene_settings, &temp_scene) != IPL_STATUS_SUCCESS) {
        iplContextRelease(&export_context);
        return ERR_CANT_CREATE;
    }

    IPLStaticMesh temp_mesh = nullptr;
    if (iplStaticMeshCreate(temp_scene, &mesh_settings, &temp_mesh) != IPL_STATUS_SUCCESS) {
        iplSceneRelease(&temp_scene);
        iplContextRelease(&export_context);
        return ERR_CANT_CREATE;
    }

    iplStaticMeshAdd(temp_mesh, temp_scene);
    iplSceneCommit(temp_scene);

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
    Error save_err = ResourceSaver::get_singleton()->save(asset, path, ResourceSaver::FLAG_CHANGE_PATH);
    if (save_err == OK && Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
        UtilityFunctions::print("Nexus Resonance: Static scene exported to ", path, " (", (int)ipl_triangles.size(), " triangles).");
    }
    return save_err;
}

int64_t ResonanceSceneManager::get_static_scene_hash(Node* scene_root, std::function<uint64_t(const PackedByteArray&)> hash_fn) {
    if (!scene_root) return 0;
    std::vector<IPLVector3> ipl_vertices;
    std::vector<IPLTriangle> ipl_triangles;
    collect_static_mesh_data(scene_root, ipl_vertices, ipl_triangles, nullptr);
    if (ipl_triangles.empty()) return 0;

    PackedByteArray pba;
    pba.resize((int)(ipl_vertices.size() * sizeof(IPLVector3) + ipl_triangles.size() * sizeof(IPLTriangle)));
    uint8_t* w = pba.ptrw();
    memcpy(w, ipl_vertices.data(), ipl_vertices.size() * sizeof(IPLVector3));
    memcpy(w + ipl_vertices.size() * sizeof(IPLVector3), ipl_triangles.data(), ipl_triangles.size() * sizeof(IPLTriangle));
    return (int64_t)hash_fn(pba);
}

} // namespace godot

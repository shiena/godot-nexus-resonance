#include "resonance_geometry.h"
#include "resonance_constants.h"
#include "resonance_debug_log.h"
#include "resonance_geometry_asset.h"
#include "resonance_log.h"
#include "resonance_server.h"
#include "resonance_static_scene.h"
#include "resonance_utils.h"
#include <cstdint>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <phonon.h>
#include <vector>

using namespace godot;

static IPLMaterial get_default_ipl_material() {
    IPLMaterial m{};
    m.absorption[0] = resonance::kSceneExportAbsorptionLow;
    m.absorption[1] = resonance::kSceneExportAbsorptionMid;
    m.absorption[2] = resonance::kSceneExportAbsorptionHigh;
    m.scattering = resonance::kSceneExportScattering;
    m.transmission[0] = m.transmission[1] = m.transmission[2] = resonance::kSceneExportTransmission;
    return m;
}

/// Parse Godot mesh to IPL vertices/triangles. Returns true if at least one triangle was added.
static bool parse_mesh_to_ipl(const Ref<Mesh>& mesh, const Transform3D& xform,
                              std::vector<IPLVector3>& out_vertices, std::vector<IPLTriangle>& out_triangles,
                              std::vector<IPLint32>& out_mat_indices) {
    if (mesh.is_null())
        return false;
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
                out_mat_indices.push_back(0);
            }
        } else {
            for (int v = 0; v < vertices.size(); v += 3) {
                if (v + 2 >= vertices.size())
                    break;
                out_triangles.push_back({(int)v + (int)v_offset,
                                         (int)(v + 1) + (int)v_offset,
                                         (int)(v + 2) + (int)v_offset});
                out_mat_indices.push_back(0);
            }
        }
    }
    return !out_triangles.empty();
}

ResonanceGeometry::ResonanceGeometry() {}

ResonanceGeometry::~ResonanceGeometry() {
    _clear_meshes();
}

void ResonanceGeometry::_ready() {
    add_to_group("resonance_geometry");
    _propagate_material_and_geometry_to_descendants();
    _create_meshes();
    _update_viz_geometry_override();
}

void ResonanceGeometry::_exit_tree() {
    server_init_retry_pending_ = false;
    server_init_retry_count_ = 0;
    // Godot frees child nodes automatically when parent is removed; do not queue_free.
    viz_geometry_override = nullptr;
    // Skip clear in editor: modifying IPL scene during scene switch (after bake) can crash.
    // Destructor still runs _clear_meshes when node is freed.
    if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    _clear_meshes();
}

void ResonanceGeometry::_notification(int p_what) {
    if (p_what == NOTIFICATION_TRANSFORM_CHANGED) {
        if (dynamic_object && is_inside_tree()) {
            _update_dynamic_transform();
        }
    }
}

void ResonanceGeometry::_schedule_retry_create_meshes_when_server_ready() {
    Engine* eng = Engine::get_singleton();
    if (eng && eng->is_editor_hint())
        return;
    if (!is_inside_tree())
        return;
    if (server_init_retry_pending_)
        return;
    server_init_retry_pending_ = true;
    call_deferred("_deferred_retry_create_meshes");
}

void ResonanceGeometry::_deferred_retry_create_meshes() {
    server_init_retry_pending_ = false;
    Engine* eng = Engine::get_singleton();
    if (eng && eng->is_editor_hint())
        return;
    if (!is_inside_tree())
        return;
    ResonanceServer* server = ResonanceServer::get_singleton();
    if (!server || !server->is_initialized()) {
        if (server_init_retry_count_ < kMaxServerInitRetries) {
            server_init_retry_count_++;
            server_init_retry_pending_ = true;
            call_deferred("_deferred_retry_create_meshes");
        }
        return;
    }
    server_init_retry_count_ = 0;
    _create_meshes();
    _update_viz_geometry_override();
}

void ResonanceGeometry::set_dynamic(bool p_dynamic) {
    if (dynamic_object != p_dynamic) {
        dynamic_object = p_dynamic;
        if (is_inside_tree())
            _create_meshes();
        set_notify_transform(dynamic_object);
    }
}

bool ResonanceGeometry::is_dynamic() const { return dynamic_object; }

void ResonanceGeometry::set_mesh_asset(const Ref<ResonanceGeometryAsset>& p_asset) {
    if (mesh_asset != p_asset) {
        mesh_asset = p_asset;
        if (is_inside_tree() && dynamic_object)
            _create_meshes();
    }
}

Ref<ResonanceGeometryAsset> ResonanceGeometry::get_mesh_asset() const {
    return mesh_asset;
}

void ResonanceGeometry::set_geometry_override(const Ref<Mesh>& p_mesh) {
    if (geometry_override != p_mesh) {
        geometry_override = p_mesh;
        if (is_inside_tree())
            _create_meshes();
        _update_viz_geometry_override();
    }
}

Ref<Mesh> ResonanceGeometry::get_geometry_override() const {
    return geometry_override;
}

void ResonanceGeometry::set_show_geometry_override_in_viewport(bool p_show) {
    if (show_geometry_override_in_viewport != p_show) {
        show_geometry_override_in_viewport = p_show;
        _update_viz_geometry_override();
    }
}

bool ResonanceGeometry::is_show_geometry_override_in_viewport() const {
    return show_geometry_override_in_viewport;
}

void ResonanceGeometry::set_export_all_children(bool p_export) {
    if (export_all_children != p_export) {
        export_all_children = p_export;
    }
}

bool ResonanceGeometry::get_export_all_children() const {
    return export_all_children;
}

void ResonanceGeometry::_update_viz_geometry_override() {
    if (!Engine::get_singleton() || !Engine::get_singleton()->is_editor_hint())
        return;

    if (!show_geometry_override_in_viewport || !geometry_override.is_valid()) {
        if (viz_geometry_override) {
            viz_geometry_override->queue_free();
            viz_geometry_override = nullptr;
        }
        return;
    }

    if (!viz_geometry_override) {
        viz_geometry_override = memnew(MeshInstance3D);
        viz_geometry_override->set_name("VizGeometryOverride");
        viz_geometry_override->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        Ref<StandardMaterial3D> mat;
        mat.instantiate();
        mat->set_albedo(Color(resonance::kGeometryOverrideVizR, resonance::kGeometryOverrideVizG, resonance::kGeometryOverrideVizB, resonance::kGeometryOverrideVizA));
        mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        viz_geometry_override->set_material_override(mat);
        add_child(viz_geometry_override);
    }
    viz_geometry_override->set_mesh(geometry_override);
    viz_geometry_override->set_visible(show_geometry_override_in_viewport);
}

void ResonanceGeometry::_clear_meshes_impl() {
    ResonanceServer* server = ResonanceServer::get_singleton();

    if (server && debug_mesh_id >= 0) {
        server->unregister_debug_mesh(debug_mesh_id);
        debug_mesh_id = -1;
    }

    // Clean up Steam Audio resources. Caller must hold simulation_mutex when server is valid.
    // Dynamic geometry: static_meshes live in sub_scene; instanced_mesh is in the global scene.
    // Detach InstancedMesh from the global scene before removing static meshes from sub_scene (runtime crash otherwise).
    if (server && server->is_initialized()) {
        if (dynamic_object && instanced_mesh) {
            IPLScene global_scene_handle = server->get_scene_handle();
            iplInstancedMeshRemove(instanced_mesh, global_scene_handle);
            iplInstancedMeshRelease(&instanced_mesh);
            instanced_mesh = nullptr;
            // Phonon: InstancedMeshRemove requires iplSceneCommit on the parent scene before other scene edits.
            if (global_scene_handle)
                iplSceneCommit(global_scene_handle);
        }

        IPLScene scene_for_static = dynamic_object ? sub_scene : server->get_scene_handle();
        // Dynamic sub_scene: after InstancedMesh detach+commit on global scene, per-mesh Remove/Release on
        // sub_scene can crash (Phonon 4.8.x). Drop handles and release the sub_scene only.
        if (dynamic_object && sub_scene) {
            static_meshes.clear();
        } else {
            for (auto& mesh : static_meshes) {
                if (scene_for_static)
                    iplStaticMeshRemove(mesh, scene_for_static);
                iplStaticMeshRelease(&mesh);
            }
            static_meshes.clear();
        }

        if (instanced_mesh) {
            iplInstancedMeshRemove(instanced_mesh, server->get_scene_handle());
            iplInstancedMeshRelease(&instanced_mesh);
        }

        // Notify change if we removed triangles (caller holds simulation_mutex via _clear_meshes)
        if (triangle_count > 0) {
            server->notify_geometry_changed_assume_locked(-triangle_count);
        }
    } else {
        // Fallback cleanup (just release handles, don't touch scene)
        if (dynamic_object && instanced_mesh) {
            iplInstancedMeshRelease(&instanced_mesh);
            instanced_mesh = nullptr;
        }
        if (dynamic_object && sub_scene) {
            static_meshes.clear();
        } else {
            for (auto& mesh : static_meshes)
                iplStaticMeshRelease(&mesh);
            static_meshes.clear();
        }
        if (instanced_mesh)
            iplInstancedMeshRelease(&instanced_mesh);
    }

    // Sub-scene belongs to this object, not the global scene directly
    if (sub_scene) {
        if (dynamic_object)
            iplSceneCommit(sub_scene);
        iplSceneRelease(&sub_scene);
        sub_scene = nullptr;
    }

    static_meshes.clear();
    instanced_mesh = nullptr;
    triangle_count = 0;
}

void ResonanceGeometry::_clear_meshes() {
    ResonanceServer* server = ResonanceServer::get_singleton();
    if (server && server->is_initialized()) {
        auto lock = server->scoped_simulation_lock();
        _clear_meshes_impl();
    } else {
        _clear_meshes_impl();
    }
}

static bool _scene_has_static_scene_asset(Node* node) {
    if (!node)
        return false;
    if (node->is_class("ResonanceStaticScene")) {
        ResonanceStaticScene* ss = Object::cast_to<ResonanceStaticScene>(node);
        if (ss && ss->has_valid_asset())
            return true;
    }
    for (int i = 0; i < node->get_child_count(); i++) {
        if (_scene_has_static_scene_asset(node->get_child(i)))
            return true;
    }
    return false;
}

void ResonanceGeometry::_create_meshes() {
    Node* parent = get_parent();
    Node3D* node3d = Object::cast_to<Node3D>(parent);
    if (!node3d || !node3d->is_visible_in_tree())
        return;

    // static geometry is in the exported scene; skip when ResonanceStaticScene provides it
    if (!dynamic_object && (!Engine::get_singleton() || !Engine::get_singleton()->is_editor_hint())) {
        Node* root = this;
        while (root->get_parent())
            root = root->get_parent();
        if (_scene_has_static_scene_asset(root))
            return;
    }

    const bool use_asset_path = dynamic_object && mesh_asset.is_valid() && mesh_asset->is_valid();
    MeshInstance3D* mesh_instance = Object::cast_to<MeshInstance3D>(parent);

    Ref<Mesh> mesh_for_geom;
    if (!use_asset_path) {
        if (geometry_override.is_valid()) {
            mesh_for_geom = geometry_override;
        } else if (mesh_instance) {
            mesh_for_geom = mesh_instance->get_mesh();
        }
        if (mesh_for_geom.is_null())
            return;
    }

    ResonanceServer* server = ResonanceServer::get_singleton();
    if (!server || !server->is_initialized()) {
        _schedule_retry_create_meshes_when_server_ready();
        return;
    }
    server_init_retry_count_ = 0;

    Transform3D xform = dynamic_object ? Transform3D() : node3d->get_global_transform();

    if (use_asset_path) {
        auto lock = server->scoped_simulation_lock();
        _clear_meshes_impl();
        // --- Load from serialized asset (iplStaticMeshLoad) ---
        // Sub-scene must match global scene ray tracer (Embree / Default / Radeon Rays); see ResonanceServer::_init_scene_and_simulator.
        IPLSceneSettings sceneSettings{};
        sceneSettings.type = server->get_scene_type();
        sceneSettings.embreeDevice = server->get_embree_device_handle();
        sceneSettings.radeonRaysDevice = server->get_radeon_rays_device_handle();

        if (iplSceneCreate(server->get_context_handle(), &sceneSettings, &sub_scene) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceGeometry: iplSceneCreate failed (asset path).");
            return;
        }

        // Copy mesh data to avoid const_cast: Phonon API expects IPLbyte*; we pass our copy to prevent
        // any risk of the API modifying read-only asset data.
        PackedByteArray mesh_copy = mesh_asset->get_mesh_data();
        IPLSerializedObjectSettings serialSettings{};
        serialSettings.data = reinterpret_cast<IPLbyte*>(mesh_copy.ptrw());
        serialSettings.size = static_cast<IPLsize>(mesh_copy.size());

        IPLSerializedObject serialObj = nullptr;
        if (iplSerializedObjectCreate(server->get_context_handle(), &serialSettings, &serialObj) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceGeometry: iplSerializedObjectCreate failed.");
            iplSceneRelease(&sub_scene);
            sub_scene = nullptr;
            return;
        }

        IPLStaticMesh local_mesh = nullptr;
        IPLerror loadErr = iplStaticMeshLoad(sub_scene, serialObj, nullptr, nullptr, &local_mesh);
        iplSerializedObjectRelease(&serialObj);

        if (loadErr != IPL_STATUS_SUCCESS || !local_mesh) {
            ResonanceLog::error("ResonanceGeometry: iplStaticMeshLoad failed.");
            iplSceneRelease(&sub_scene);
            sub_scene = nullptr;
            return;
        }

        iplStaticMeshAdd(local_mesh, sub_scene);
        static_meshes.push_back(local_mesh);

        if (material.is_valid()) {
            IPLMaterial mat = material->get_ipl_material();
            iplStaticMeshSetMaterial(local_mesh, sub_scene, &mat, 0);
        }

        iplSceneCommit(sub_scene);

        IPLInstancedMeshSettings instSettings{};
        instSettings.subScene = sub_scene;
        instSettings.transform = ResonanceUtils::to_ipl_matrix(node3d->get_global_transform());

        if (iplInstancedMeshCreate(server->get_scene_handle(), &instSettings, &instanced_mesh) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceGeometry: iplInstancedMeshCreate failed (asset path).");
            iplStaticMeshRemove(local_mesh, sub_scene);
            iplStaticMeshRelease(&local_mesh);
            iplSceneRelease(&sub_scene);
            sub_scene = nullptr;
            static_meshes.clear();
        } else {
            iplInstancedMeshAdd(instanced_mesh, server->get_scene_handle());
            triangle_count = mesh_asset->get_triangle_count();
        }
    } else {
        // --- Runtime mesh parsing: parse outside lock to reduce lock duration ---
        Ref<Mesh> mesh = mesh_for_geom;
        std::vector<IPLVector3> ipl_vertices;
        std::vector<IPLTriangle> ipl_triangles;
        std::vector<IPLint32> ipl_mat_indices;

        if (!parse_mesh_to_ipl(mesh, xform, ipl_vertices, ipl_triangles, ipl_mat_indices))
            return;

        IPLMaterial mat_settings = material.is_valid() ? material->get_ipl_material() : get_default_ipl_material();

        IPLStaticMeshSettings mesh_settings{};
        mesh_settings.numVertices = (IPLint32)ipl_vertices.size();
        mesh_settings.numTriangles = (IPLint32)ipl_triangles.size();
        mesh_settings.numMaterials = 1;
        mesh_settings.vertices = ipl_vertices.data();
        mesh_settings.triangles = ipl_triangles.data();
        mesh_settings.materials = &mat_settings;
        mesh_settings.materialIndices = ipl_mat_indices.data();

        {
            auto lock = server->scoped_simulation_lock();
            _clear_meshes_impl();

            if (dynamic_object) {
                IPLSceneSettings sceneSettings{};
                sceneSettings.type = server->get_scene_type();
                sceneSettings.embreeDevice = server->get_embree_device_handle();
                sceneSettings.radeonRaysDevice = server->get_radeon_rays_device_handle();

                if (iplSceneCreate(server->get_context_handle(), &sceneSettings, &sub_scene) == IPL_STATUS_SUCCESS) {

                    IPLStaticMesh local_mesh = nullptr;
                    if (iplStaticMeshCreate(sub_scene, &mesh_settings, &local_mesh) == IPL_STATUS_SUCCESS) {

                        iplStaticMeshAdd(local_mesh, sub_scene);
                        static_meshes.push_back(local_mesh);
                        iplSceneCommit(sub_scene);

                        IPLInstancedMeshSettings instSettings{};
                        instSettings.subScene = sub_scene;
                        instSettings.transform = ResonanceUtils::to_ipl_matrix(node3d->get_global_transform());

                        if (iplInstancedMeshCreate(server->get_scene_handle(), &instSettings, &instanced_mesh) != IPL_STATUS_SUCCESS) {
                            ResonanceLog::error("ResonanceGeometry: iplInstancedMeshCreate failed (dynamic).");
                            iplStaticMeshRemove(local_mesh, sub_scene);
                            iplStaticMeshRelease(&local_mesh);
                            iplSceneRelease(&sub_scene);
                            sub_scene = nullptr;
                            static_meshes.clear();
                        } else {
                            iplInstancedMeshAdd(instanced_mesh, server->get_scene_handle());
                            triangle_count += (int)ipl_triangles.size();
                        }
                    } else {
                        ResonanceLog::error("ResonanceGeometry: iplStaticMeshCreate failed (dynamic).");
                        iplSceneRelease(&sub_scene);
                        sub_scene = nullptr;
                    }
                } else {
                    ResonanceLog::error("ResonanceGeometry: iplSceneCreate failed (dynamic).");
                }
            } else {
                IPLStaticMesh new_mesh = nullptr;
                if (iplStaticMeshCreate(server->get_scene_handle(), &mesh_settings, &new_mesh) != IPL_STATUS_SUCCESS) {
                    ResonanceLog::error("ResonanceGeometry: iplStaticMeshCreate failed (static).");
                } else {
                    iplStaticMeshAdd(new_mesh, server->get_scene_handle());
                    static_meshes.push_back(new_mesh);
                    triangle_count += (int)ipl_triangles.size();
                }
            }

            if (triangle_count > 0 && server->wants_debug_reflection_viz()) {
                Transform3D tr = dynamic_object ? node3d->get_global_transform() : Transform3D();
                IPLMatrix4x4 ipl_xform = ResonanceUtils::to_ipl_matrix(tr);
                debug_mesh_id = server->register_debug_mesh(ipl_vertices, ipl_triangles, ipl_mat_indices.data(), &ipl_xform, &mat_settings);
                resonance::debug_log_raw("resonance_geometry:register_debug", "mesh_registered", triangle_count, debug_mesh_id);
            }
        }
    }

    if (triangle_count > 0) {
        server->notify_geometry_changed(triangle_count);
    }
}

void ResonanceGeometry::_update_dynamic_transform() {
    if (!instanced_mesh)
        return;

    Node* parent = get_parent();
    Node3D* node3d = Object::cast_to<Node3D>(parent);
    if (!node3d)
        return;

    ResonanceServer* server = ResonanceServer::get_singleton();
    if (!server)
        return;

    IPLMatrix4x4 mat = ResonanceUtils::to_ipl_matrix(node3d->get_global_transform());

    {
        auto lock = server->scoped_simulation_lock();
        iplInstancedMeshUpdateTransform(instanced_mesh, server->get_scene_handle(), mat);
    }

    // Notify dirty (0 delta means count didn't change, but position did -> needs Commit)
    server->notify_geometry_changed(0);
}

static void _propagate_recursive(Node* from, const Ref<ResonanceMaterial>& mat, const Ref<Mesh>& geom_override) {
    if (!from)
        return;
    for (int i = 0; i < from->get_child_count(); i++) {
        Node* c = from->get_child(i);
        if (c->is_class("ResonanceGeometry")) {
            ResonanceGeometry* child_geom = Object::cast_to<ResonanceGeometry>(c);
            if (child_geom) {
                if (mat.is_valid() && child_geom->get_material().is_null())
                    child_geom->set_material(mat);
                if (geom_override.is_valid() && child_geom->get_geometry_override().is_null())
                    child_geom->set_geometry_override(geom_override);
            }
        }
        _propagate_recursive(c, mat, geom_override);
    }
}

void ResonanceGeometry::_propagate_material_and_geometry_to_descendants() {
    if (!material.is_valid() && !geometry_override.is_valid())
        return;
    _propagate_recursive(this, material, geometry_override);
}

void ResonanceGeometry::set_material(const Ref<ResonanceMaterial>& p_material) { material = p_material; }
Ref<ResonanceMaterial> ResonanceGeometry::get_material() const { return material; }

Error ResonanceGeometry::export_dynamic_mesh_to_asset(const String& p_path) {
    Node* parent = get_parent();
    Node3D* node3d = Object::cast_to<Node3D>(parent);
    if (!parent || !node3d || !node3d->is_visible_in_tree())
        return ERR_INVALID_PARAMETER;

    MeshInstance3D* mesh_instance = Object::cast_to<MeshInstance3D>(parent);
    Ref<Mesh> mesh = geometry_override.is_valid() ? geometry_override : (mesh_instance ? mesh_instance->get_mesh() : Ref<Mesh>());
    if (mesh.is_null())
        return ERR_INVALID_PARAMETER;

    std::vector<IPLVector3> ipl_vertices;
    std::vector<IPLTriangle> ipl_triangles;
    std::vector<IPLint32> ipl_mat_indices;

    Transform3D xform; // Identity for dynamic (local space)
    if (!parse_mesh_to_ipl(mesh, xform, ipl_vertices, ipl_triangles, ipl_mat_indices))
        return ERR_INVALID_PARAMETER;

    IPLMaterial mat_settings = material.is_valid() ? material->get_ipl_material() : get_default_ipl_material();

    IPLStaticMeshSettings mesh_settings{};
    mesh_settings.numVertices = (IPLint32)ipl_vertices.size();
    mesh_settings.numTriangles = (IPLint32)ipl_triangles.size();
    mesh_settings.numMaterials = 1;
    mesh_settings.vertices = ipl_vertices.data();
    mesh_settings.triangles = ipl_triangles.data();
    mesh_settings.materials = &mat_settings;
    mesh_settings.materialIndices = ipl_mat_indices.data();

    // Standalone export: create minimal Phonon context/scene (no ResonanceServer required)
    IPLContext export_context = nullptr;
    IPLContextSettings ctx_settings{};
    ctx_settings.version = STEAMAUDIO_VERSION;
    if (iplContextCreate(&ctx_settings, &export_context) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceGeometry: iplContextCreate failed (export_dynamic_mesh_to_asset).");
        return ERR_CANT_CREATE;
    }

    IPLSceneSettings scene_settings{};
    scene_settings.type = IPL_SCENETYPE_DEFAULT;

    IPLScene temp_scene = nullptr;
    if (iplSceneCreate(export_context, &scene_settings, &temp_scene) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceGeometry: iplSceneCreate failed (export_dynamic_mesh_to_asset).");
        iplContextRelease(&export_context);
        return ERR_CANT_CREATE;
    }

    IPLStaticMesh temp_mesh = nullptr;
    if (iplStaticMeshCreate(temp_scene, &mesh_settings, &temp_mesh) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceGeometry: iplStaticMeshCreate failed (export_dynamic_mesh_to_asset).");
        iplSceneRelease(&temp_scene);
        iplContextRelease(&export_context);
        return ERR_CANT_CREATE;
    }

    iplStaticMeshAdd(temp_mesh, temp_scene);
    iplSceneCommit(temp_scene);

    IPLSerializedObjectSettings serial_settings{};
    IPLSerializedObject serial_obj = nullptr;
    if (iplSerializedObjectCreate(export_context, &serial_settings, &serial_obj) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceGeometry: iplSerializedObjectCreate failed (export_dynamic_mesh_to_asset).");
        iplStaticMeshRelease(&temp_mesh);
        iplSceneRelease(&temp_scene);
        iplContextRelease(&export_context);
        return ERR_CANT_CREATE;
    }

    iplStaticMeshSave(temp_mesh, serial_obj);

    IPLsize size = iplSerializedObjectGetSize(serial_obj);
    IPLbyte* data = iplSerializedObjectGetData(serial_obj);
    if (!data || size == 0) {
        ResonanceLog::error("ResonanceGeometry: iplStaticMeshSave produced no data (export_dynamic_mesh_to_asset).");
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
    if (save_err == OK) {
        mesh_asset = asset;
        if (is_inside_tree() && dynamic_object)
            _create_meshes();
    }
    return save_err;
}

void ResonanceGeometry::refresh_geometry() {
    _create_meshes();
}

void ResonanceGeometry::discard_meshes_before_scene_release() {
    ResonanceServer* server = ResonanceServer::get_singleton();
    if (server && debug_mesh_id >= 0) {
        server->unregister_debug_mesh(debug_mesh_id);
        debug_mesh_id = -1;
    }

    // Release IPL resources before scene teardown. Phonon uses refcounting; meshes must be
    // explicitly removed and released. Static meshes are in global scene (static path) or sub_scene (dynamic path).
    if (server && server->is_initialized()) {
        auto lock = server->scoped_simulation_lock();
        if (dynamic_object && instanced_mesh) {
            IPLScene global_scene_handle = server->get_scene_handle();
            iplInstancedMeshRemove(instanced_mesh, global_scene_handle);
            iplInstancedMeshRelease(&instanced_mesh);
            instanced_mesh = nullptr;
            if (global_scene_handle)
                iplSceneCommit(global_scene_handle);
        }
        IPLScene scene_for_static = dynamic_object ? sub_scene : server->get_scene_handle();
        if (dynamic_object && sub_scene) {
            static_meshes.clear();
        } else {
            for (auto& mesh : static_meshes) {
                if (scene_for_static)
                    iplStaticMeshRemove(mesh, scene_for_static);
                iplStaticMeshRelease(&mesh);
            }
            static_meshes.clear();
        }
        if (instanced_mesh) {
            iplInstancedMeshRemove(instanced_mesh, server->get_scene_handle());
            iplInstancedMeshRelease(&instanced_mesh);
        }
        if (triangle_count > 0) {
            server->notify_geometry_changed_assume_locked(-triangle_count);
        }
        if (sub_scene) {
            if (dynamic_object)
                iplSceneCommit(sub_scene);
            iplSceneRelease(&sub_scene);
            sub_scene = nullptr;
        }
    } else {
        if (dynamic_object && instanced_mesh) {
            iplInstancedMeshRelease(&instanced_mesh);
            instanced_mesh = nullptr;
        }
        if (dynamic_object && sub_scene) {
            static_meshes.clear();
        } else {
            for (auto& mesh : static_meshes) {
                iplStaticMeshRelease(&mesh);
            }
            static_meshes.clear();
        }
        if (instanced_mesh) {
            iplInstancedMeshRelease(&instanced_mesh);
        }
        if (sub_scene) {
            iplSceneRelease(&sub_scene);
            sub_scene = nullptr;
        }
    }

    static_meshes.clear();
    instanced_mesh = nullptr;
    triangle_count = 0;
}

void ResonanceGeometry::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_material", "p_material"), &ResonanceGeometry::set_material);
    ClassDB::bind_method(D_METHOD("get_material"), &ResonanceGeometry::get_material);
    ClassDB::bind_method(D_METHOD("refresh_geometry"), &ResonanceGeometry::refresh_geometry);
    ClassDB::bind_method(D_METHOD("_deferred_retry_create_meshes"), &ResonanceGeometry::_deferred_retry_create_meshes);
    ClassDB::bind_method(D_METHOD("discard_meshes_before_scene_release"), &ResonanceGeometry::discard_meshes_before_scene_release);
    ClassDB::bind_method(D_METHOD("set_dynamic", "p_dynamic"), &ResonanceGeometry::set_dynamic);
    ClassDB::bind_method(D_METHOD("is_dynamic"), &ResonanceGeometry::is_dynamic);
    ClassDB::bind_method(D_METHOD("set_mesh_asset", "p_asset"), &ResonanceGeometry::set_mesh_asset);
    ClassDB::bind_method(D_METHOD("get_mesh_asset"), &ResonanceGeometry::get_mesh_asset);
    ClassDB::bind_method(D_METHOD("set_geometry_override", "p_mesh"), &ResonanceGeometry::set_geometry_override);
    ClassDB::bind_method(D_METHOD("get_geometry_override"), &ResonanceGeometry::get_geometry_override);
    ClassDB::bind_method(D_METHOD("set_show_geometry_override_in_viewport", "p_show"), &ResonanceGeometry::set_show_geometry_override_in_viewport);
    ClassDB::bind_method(D_METHOD("is_show_geometry_override_in_viewport"), &ResonanceGeometry::is_show_geometry_override_in_viewport);
    ClassDB::bind_method(D_METHOD("set_export_all_children", "p_export"), &ResonanceGeometry::set_export_all_children);
    ClassDB::bind_method(D_METHOD("get_export_all_children"), &ResonanceGeometry::get_export_all_children);
    ClassDB::bind_method(D_METHOD("export_dynamic_mesh_to_asset", "p_path"), &ResonanceGeometry::export_dynamic_mesh_to_asset);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ResonanceMaterial"), "set_material", "get_material");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh_asset", PROPERTY_HINT_RESOURCE_TYPE, "ResonanceGeometryAsset"), "set_mesh_asset", "get_mesh_asset");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "geometry_override", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_geometry_override", "get_geometry_override");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_geometry_override_in_viewport"), "set_show_geometry_override_in_viewport", "is_show_geometry_override_in_viewport");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "export_all_children"), "set_export_all_children", "get_export_all_children");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dynamic"), "set_dynamic", "is_dynamic");
}

void ResonanceGeometry::_validate_property(PropertyInfo& p_property) const {
    Node3D::_validate_property(p_property);
    if (p_property.name == StringName("export_all_children") && dynamic_object) {
        p_property.usage = PROPERTY_USAGE_NO_EDITOR;
    }
}
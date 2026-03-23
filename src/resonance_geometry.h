#ifndef RESONANCE_GEOMETRY_H
#define RESONANCE_GEOMETRY_H

#include "resonance_geometry_asset.h"
#include "resonance_material.h"
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <phonon.h>
#include <vector>

namespace godot {

class ResonanceGeometry : public Node3D {
    GDCLASS(ResonanceGeometry, Node3D)

  private:
    Ref<ResonanceMaterial> material;
    Ref<ResonanceGeometryAsset> mesh_asset;
    Ref<Mesh> geometry_override;
    bool dynamic_object = false;
    bool show_geometry_override_in_viewport = false;
    bool export_all_children = false;

    MeshInstance3D* viz_geometry_override = nullptr; // Editor-only: shows geometry_override in viewport

    // Static Path
    std::vector<IPLStaticMesh> static_meshes;

    // Dynamic Path
    IPLScene sub_scene = nullptr;              // Local scene containing the mesh
    IPLInstancedMesh instanced_mesh = nullptr; // The instance placed in the global scene

    int triangle_count = 0;
    int debug_mesh_id = -1; // RayTraceDebugContext mesh id for unregister

    /// When _create_meshes runs before ResonanceServer::init_audio_engine, retry on later frames.
    static constexpr int kMaxServerInitRetries = 64;
    bool server_init_retry_pending_ = false;
    int server_init_retry_count_ = 0;

    void _create_meshes();
    void _schedule_retry_create_meshes_when_server_ready();
    void _deferred_retry_create_meshes();
    void _clear_meshes();
    void _propagate_material_and_geometry_to_descendants();
    /// Internal: cleanup without locking. Caller must hold simulation lock when touching scene.
    void _clear_meshes_impl();
    void _update_dynamic_transform();

  protected:
    static void _bind_methods();
    void _notification(int p_what); // To catch Transform Changed
    virtual void _validate_property(PropertyInfo& p_property) const;

  public:
    ResonanceGeometry();
    ~ResonanceGeometry();

    ResonanceGeometry(const ResonanceGeometry&) = delete;
    ResonanceGeometry(ResonanceGeometry&&) = delete;

    void _ready() override;
    void _exit_tree() override;

    void set_material(const Ref<ResonanceMaterial>& p_material);
    Ref<ResonanceMaterial> get_material() const;

    /// Call to re-register geometry with the server (e.g. after server init in editor baking)
    void refresh_geometry();

    /// Clear mesh references without releasing (call before reinit; scene release handles cleanup)
    void discard_meshes_before_scene_release();

    // Dynamic Setter
    void set_dynamic(bool p_dynamic);
    bool is_dynamic() const;

    void set_mesh_asset(const Ref<ResonanceGeometryAsset>& p_asset);
    Ref<ResonanceGeometryAsset> get_mesh_asset() const;

    void set_geometry_override(const Ref<Mesh>& p_mesh);
    Ref<Mesh> get_geometry_override() const;

    void set_show_geometry_override_in_viewport(bool p_show);
    bool is_show_geometry_override_in_viewport() const;

    /// When true, child ResonanceGeometry nodes are included during static scene export. No effect for dynamic objects.
    void set_export_all_children(bool p_export);
    bool get_export_all_children() const;

    void _update_viz_geometry_override(); // Editor-only: show geometry_override in viewport

    /// Export parent MeshInstance3D's mesh (or geometry_override if set) to a ResonanceGeometryAsset (serialized format).
    /// Works in Editor without running the scene. Saves to p_path and assigns to mesh_asset. Returns OK on success.
    Error export_dynamic_mesh_to_asset(const String& p_path);
};

} // namespace godot

#endif // RESONANCE_GEOMETRY_H
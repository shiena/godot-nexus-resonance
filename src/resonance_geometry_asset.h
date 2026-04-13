#ifndef RESONANCE_GEOMETRY_ASSET_H
#define RESONANCE_GEOMETRY_ASSET_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace godot {

/// Holds serialized Steam Audio mesh data (from iplStaticMeshSave).
/// Use with ResonanceGeometry when dynamic=true for export/load workflow:
/// better stability (Phonon native format) and faster runtime load.
/// For static: merged geometry from multiple ResonanceStaticGeometry nodes (one asset per scene).
/// For dynamic: single mesh per object (one asset per ResonanceDynamicGeometry).
class ResonanceGeometryAsset : public Resource {
    GDCLASS(ResonanceGeometryAsset, Resource)

  private:
    PackedByteArray mesh_data;
    int triangle_count = 0;            // Set during export; used for notify_geometry_changed when loading
    PackedVector3Array debug_vertices; // For reflection ray debug viz (set during export)
    PackedInt32Array debug_triangles;  // Flat: 3 indices per triangle (set during export)

  protected:
    static void _bind_methods();

  public:
    ResonanceGeometryAsset();
    ~ResonanceGeometryAsset();

    void set_mesh_data(const PackedByteArray& p_data);
    /// Copy of serialized mesh bytes (can be large). For Phonon load prefer get_data_ptr / get_size to avoid copying.
    PackedByteArray get_mesh_data() const;

    void set_triangle_count(int p_count);
    int get_triangle_count() const { return triangle_count; }

    void set_debug_vertices(const PackedVector3Array& p_verts);
    PackedVector3Array get_debug_vertices() const { return debug_vertices; }
    void set_debug_triangles(const PackedInt32Array& p_tris);
    PackedInt32Array get_debug_triangles() const { return debug_triangles; }
    bool has_debug_geometry() const { return debug_vertices.size() >= 3 && debug_triangles.size() >= 3; }

    /// Returns raw pointer for efficient access. Valid until mesh_data is modified.
    const uint8_t* get_data_ptr() const;
    int64_t get_size() const;

    bool is_valid() const { return !mesh_data.is_empty(); }
};

} // namespace godot

#endif

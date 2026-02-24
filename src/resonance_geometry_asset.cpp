#include "resonance_geometry_asset.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

ResonanceGeometryAsset::ResonanceGeometryAsset() {}
ResonanceGeometryAsset::~ResonanceGeometryAsset() {}

void ResonanceGeometryAsset::set_mesh_data(const PackedByteArray& p_data) {
    mesh_data = p_data;
}

PackedByteArray ResonanceGeometryAsset::get_mesh_data() const {
    return mesh_data;
}

void ResonanceGeometryAsset::set_triangle_count(int p_count) {
    triangle_count = p_count >= 0 ? p_count : 0;
}

void ResonanceGeometryAsset::set_debug_vertices(const PackedVector3Array& p_verts) {
    debug_vertices = p_verts;
}

void ResonanceGeometryAsset::set_debug_triangles(const PackedInt32Array& p_tris) {
    debug_triangles = p_tris;
}

const uint8_t* ResonanceGeometryAsset::get_data_ptr() const {
    return mesh_data.ptr();
}

int64_t ResonanceGeometryAsset::get_size() const {
    return mesh_data.size();
}

void ResonanceGeometryAsset::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_mesh_data", "p_data"), &ResonanceGeometryAsset::set_mesh_data);
    ClassDB::bind_method(D_METHOD("get_mesh_data"), &ResonanceGeometryAsset::get_mesh_data);
    ClassDB::bind_method(D_METHOD("set_triangle_count", "p_count"), &ResonanceGeometryAsset::set_triangle_count);
    ClassDB::bind_method(D_METHOD("get_triangle_count"), &ResonanceGeometryAsset::get_triangle_count);
    ClassDB::bind_method(D_METHOD("set_debug_vertices", "p_verts"), &ResonanceGeometryAsset::set_debug_vertices);
    ClassDB::bind_method(D_METHOD("get_debug_vertices"), &ResonanceGeometryAsset::get_debug_vertices);
    ClassDB::bind_method(D_METHOD("set_debug_triangles", "p_tris"), &ResonanceGeometryAsset::set_debug_triangles);
    ClassDB::bind_method(D_METHOD("get_debug_triangles"), &ResonanceGeometryAsset::get_debug_triangles);
    ClassDB::bind_method(D_METHOD("has_debug_geometry"), &ResonanceGeometryAsset::has_debug_geometry);

    ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "mesh_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_mesh_data", "get_mesh_data");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "triangle_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_triangle_count", "get_triangle_count");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "debug_vertices", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_debug_vertices", "get_debug_vertices");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "debug_triangles", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_debug_triangles", "get_debug_triangles");
}

#ifndef RESONANCE_SCENE_MANAGER_H
#define RESONANCE_SCENE_MANAGER_H

#include <phonon.h>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <atomic>
#include <functional>
#include <vector>
#include <cstdint>

namespace godot {

class ResonanceGeometry;
class ResonanceGeometryAsset;
class RayTraceDebugContext;

/// Manages scene I/O, static meshes from assets, save/load, export, and hash.
/// Scene handle remains in ResonanceServer; this class operates on it via parameters.
class ResonanceSceneManager {
public:
    ResonanceSceneManager() = default;

    void save_scene_data(IPLContext ctx, IPLScene scene, const String& filename);
    void load_scene_data(IPLContext ctx, IPLScene* out_scene, IPLSimulator sim,
        IPLSceneType scene_type, IPLEmbreeDevice embree, IPLRadeonRaysDevice radeon,
        const String& filename, int* out_global_triangle_count);

    void add_static_scene_from_asset(IPLContext ctx, IPLScene scene, const Ref<ResonanceGeometryAsset>& asset,
        RayTraceDebugContext* debug_ctx, bool wants_debug_viz,
        std::vector<IPLStaticMesh>& runtime_meshes, int& runtime_tri_count,
        std::vector<int>& runtime_debug_ids, int* global_triangle_count, std::atomic<bool>* scene_dirty);

    void load_static_scene_from_asset(IPLContext ctx, IPLScene scene, const Ref<ResonanceGeometryAsset>& asset,
        RayTraceDebugContext* debug_ctx, bool wants_debug_viz,
        std::vector<IPLStaticMesh>& runtime_meshes, int& runtime_tri_count,
        std::vector<int>& runtime_debug_ids, int* global_triangle_count, std::atomic<bool>* scene_dirty);

    void clear_static_scenes(IPLScene scene, RayTraceDebugContext* debug_ctx,
        std::vector<IPLStaticMesh>& runtime_meshes, int& runtime_tri_count,
        std::vector<int>& runtime_debug_ids, int* global_triangle_count, std::atomic<bool>* scene_dirty);

    Error export_static_scene_to_asset(Node* scene_root, const String& path);
    int64_t get_static_scene_hash(Node* scene_root, std::function<uint64_t(const PackedByteArray&)> hash_fn);

private:
    static void collect_static_geometry_recursive(Node* node, std::vector<ResonanceGeometry*>& out);
    static void collect_static_mesh_data(Node* scene_root, std::vector<IPLVector3>& out_vertices,
        std::vector<IPLTriangle>& out_triangles, std::vector<IPLint32>* out_mat_indices);
    static int register_asset_debug_geometry(const Ref<ResonanceGeometryAsset>& asset, RayTraceDebugContext* debug_ctx);
};

} // namespace godot

#endif // RESONANCE_SCENE_MANAGER_H

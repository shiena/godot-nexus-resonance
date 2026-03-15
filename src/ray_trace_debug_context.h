#ifndef RAY_TRACE_DEBUG_CONTEXT_H
#define RAY_TRACE_DEBUG_CONTEXT_H

#include "resonance_constants.h"
#include <atomic>
#include <cstdint>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <mutex>
#include <phonon.h>
#include <unordered_map>
#include <vector>

namespace godot {

struct RayDebugSegment {
    float from_x, from_y, from_z;
    float to_x, to_y, to_z;
    int bounce_index;
};

class RayTraceDebugContext {
  public:
    static constexpr float kNormalLengthEpsilon = 1e-8f;
    static constexpr float kRayTriangleEpsilon = 1e-7f;
    static constexpr float kDefaultMaxRayDistance = 1e10f;
    static constexpr float kMinRayT = 0.001f;
    static void set_debug_log_path(const char* path);

    RayTraceDebugContext();
    ~RayTraceDebugContext();

    RayTraceDebugContext(const RayTraceDebugContext&) = delete;
    RayTraceDebugContext& operator=(const RayTraceDebugContext&) = delete;
    RayTraceDebugContext(RayTraceDebugContext&&) = delete;
    RayTraceDebugContext& operator=(RayTraceDebugContext&&) = delete;

    void clear();

    int register_mesh(const std::vector<IPLVector3>& vertices,
                      const std::vector<IPLTriangle>& triangles,
                      const IPLint32* material_indices,
                      const IPLMatrix4x4* transform,
                      const IPLMaterial* material);
    void unregister_mesh(int mesh_id);

    void trace_batch(IPLint32 num_rays, const IPLRay* rays,
                     const IPLfloat32* min_distances, const IPLfloat32* max_distances,
                     IPLHit* hits, int bounce_index);

    void push_rays_for_viz(IPLint32 num_rays, const IPLRay* rays,
                           const IPLHit* hits, int bounce_index);

    void get_segments_for_godot(Array& out_segments);

    /// Cast rays from origin in uniform directions, trace against geometry, return segments for viz.
    /// Main-thread only. Used when Embree scene + debug_reflections (no CUSTOM callbacks).
    void trace_reflection_rays_for_viz(const IPLVector3& origin, int num_rays,
                                       float max_distance, Array& out_segments);

  private:
    struct TriangleData {
        IPLVector3 v0, v1, v2;
        IPLVector3 normal;
        int material_index;
    };
    std::vector<TriangleData> triangles_;
    std::vector<IPLMaterial> materials_;
    std::unordered_map<int, int> mesh_id_to_mat_offset_;
    int next_mesh_id_ = 1;

    // Double-buffer for low-contention handoff between worker and main thread:
    // - Worker (push_rays_for_viz): reads write_buffer_index_ (relaxed), writes to segment_buffers_[idx].
    //   No mutex; worker and consumer never touch the same buffer simultaneously.
    // - Consumer (get_segments_for_viz): takes swap_mutex_, reads from the other buffer (1-write_idx),
    //   copies to out, clears it, then stores new write_idx (release) so worker switches buffers.
    // Lock order (MUST be consistent to avoid deadlock): geometry_mutex_ first, then swap_mutex_.
    std::vector<RayDebugSegment> segment_buffers_[2];
    std::atomic<int> write_buffer_index_{0};
    std::mutex swap_mutex_;
    std::mutex geometry_mutex_; // Protects triangles_, materials_, mesh_id_to_mat_offset_

    bool ray_triangle_intersect(const IPLRay& ray, float t_min, float t_max,
                                const TriangleData& tri, float& out_t, IPLVector3& out_normal);
};

} // namespace godot

#endif // RAY_TRACE_DEBUG_CONTEXT_H

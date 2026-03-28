#ifndef RESONANCE_BAKER_H
#define RESONANCE_BAKER_H

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <phonon.h>

#include "resonance_constants.h"
#include "resonance_probe_data.h"
#include "resonance_utils.h"

namespace godot {

class ResonanceBaker {
  public:
    ResonanceBaker() = default;
    ~ResonanceBaker() = default;
    ResonanceBaker(const ResonanceBaker&) = delete;
    ResonanceBaker& operator=(const ResonanceBaker&) = delete;
    ResonanceBaker(ResonanceBaker&&) = delete;
    ResonanceBaker& operator=(ResonanceBaker&&) = delete;

    // Generation type for probe placement
    enum ProbeGenType { GEN_CENTROID = 0,
                        GEN_UNIFORM_FLOOR = 1,
                        GEN_VOLUME = 2 };

    // Generate grid points in world space based on volume transform, extents, spacing, and generation type.
    PackedVector3Array generate_manual_grid(
        const Transform3D& volume_transform,
        Vector3 extents,
        float spacing,
        int generation_type,
        float height_above_floor);

    /// Bake using Steam Audio iplProbeArrayGenerateProbes + iplProbeBatchAddProbeArray (scene-aware placement).
    /// Only for GEN_CENTROID (0) and GEN_UNIFORM_FLOOR (1). Use bake_manual_grid for GEN_VOLUME (2).
    /// @param progress_callback When non-null, invoked synchronously on the calling thread during bake (bake blocks until done).
    /// @param pathing_scheduled If true, disk save is skipped; GDScript saves afterward with full pathing_params_hash.
    bool bake_with_probe_array(
        IPLContext context,
        IPLScene scene,
        IPLSceneType scene_type,
        IPLOpenCLDevice opencl_device,
        IPLRadeonRaysDevice radeon_rays_device,
        const Transform3D& volume_transform,
        Vector3 extents,
        float spacing,
        int generation_type,
        float height_above_floor,
        int num_bounces,
        int num_rays,
        int reflection_type,
        Ref<ResonanceProbeData> probe_data_res,
        void (*progress_callback)(float, void*) = nullptr,
        void* progress_user_data = nullptr,
        bool pathing_scheduled = false,
        int num_threads = 2,
        int ambisonics_order = resonance::kBakeDefaultAmbisonicsOrder);

    // Internal method to create grid points in local space, then transform to world space.
    // reflection_type: 0 = Convolution (BAKECONVOLUTION), 1 = Parametric (BAKEPARAMETRIC)
    // progress_callback: when non-null, invoked synchronously on calling thread during bake (bake blocks until done)
    // pathing_scheduled: if true, disk save skipped; GDScript saves afterward with full pathing_params_hash
    // opencl_device, radeon_rays_device: optional; required when scene_type is IPL_SCENETYPE_RADEONRAYS
    bool bake_manual_grid(
        IPLContext context,
        IPLScene scene,
        IPLSceneType scene_type,
        IPLOpenCLDevice opencl_device,
        IPLRadeonRaysDevice radeon_rays_device,
        const PackedVector3Array& points,
        int num_bounces,
        int num_rays,
        int reflection_type,
        Ref<ResonanceProbeData> probe_data_res,
        void (*progress_callback)(float, void*) = nullptr,
        void* progress_user_data = nullptr,
        bool pathing_scheduled = false,
        int num_threads = 2,
        int ambisonics_order = resonance::kBakeDefaultAmbisonicsOrder);

    /// @param progress_callback When non-null, invoked synchronously on the calling thread (bake blocks until done).
    bool bake_pathing(
        IPLContext context,
        IPLScene scene,
        Ref<ResonanceProbeData> probe_data_res,
        float vis_range = 500.0f,
        float path_range = 100.0f,
        int num_samples = 16,
        float radius = 0.5f,
        float threshold = 0.1f,
        void (*progress_callback)(float, void*) = nullptr,
        void* progress_user_data = nullptr,
        int num_threads = 2);

    /// Bake reflections with STATICSOURCE variation. Requires probe_data with existing probes (from Bake Probes).
    /// endpoint_position: world position of the static source. influence_radius: probes within this distance get data.
    /// @param progress_callback When non-null, invoked synchronously on the calling thread (bake blocks until done).
    bool bake_static_source(
        IPLContext context,
        IPLScene scene,
        IPLSceneType scene_type,
        IPLOpenCLDevice opencl_device,
        IPLRadeonRaysDevice radeon_rays_device,
        Ref<ResonanceProbeData> probe_data_res,
        Vector3 endpoint_position,
        float influence_radius,
        int num_bounces = 4,
        int num_rays = 4096,
        void (*progress_callback)(float, void*) = nullptr,
        void* progress_user_data = nullptr,
        int num_threads = 2,
        int ambisonics_order = resonance::kBakeDefaultAmbisonicsOrder);

    /// Bake reflections with STATICLISTENER variation. Requires probe_data with existing probes.
    /// endpoint_position: world position of the static listener. influence_radius: probes within this distance get data.
    /// @param progress_callback When non-null, invoked synchronously on the calling thread (bake blocks until done).
    bool bake_static_listener(
        IPLContext context,
        IPLScene scene,
        IPLSceneType scene_type,
        IPLOpenCLDevice opencl_device,
        IPLRadeonRaysDevice radeon_rays_device,
        Ref<ResonanceProbeData> probe_data_res,
        Vector3 endpoint_position,
        float influence_radius,
        int num_bounces = 4,
        int num_rays = 4096,
        void (*progress_callback)(float, void*) = nullptr,
        void* progress_user_data = nullptr,
        int num_threads = 2,
        int ambisonics_order = resonance::kBakeDefaultAmbisonicsOrder);

    /// Probe count in serialized probe data, or -1 if load fails.
    int32_t probe_data_get_num_probes(IPLContext context, Ref<ResonanceProbeData> probe_data_res) const;

    /// Removes one probe by index (Steam Audio iplProbeBatchRemoveProbe). Updates [param probe_data_res] bytes;
    /// if [code]probe_positions[/code] length matched probe count, the same index is removed. Clears pathing hash.
    /// Call [method ResonanceProbeVolume.reload_probe_batch] if this resource is loaded in the simulator.
    bool probe_data_remove_probe_at_index(IPLContext context, Ref<ResonanceProbeData> probe_data_res, int32_t index) const;

    /// Removes a baked data layer (iplProbeBatchRemoveData). [param baked_data_type]: 0 = reflections, 1 = pathing.
    /// [param variation]: 0 = reverb, 1 = static source, 2 = static listener, 3 = dynamic (pathing).
    /// For static source/listener, [param endpoint] and [param influence_radius] must match the bake sphere.
    bool probe_data_remove_baked_data_layer(IPLContext context, Ref<ResonanceProbeData> probe_data_res, int baked_data_type,
                                            int variation, Vector3 endpoint, float influence_radius) const;

  private:
    bool _bake_static_endpoint(
        IPLContext context,
        IPLScene scene,
        IPLSceneType scene_type,
        IPLOpenCLDevice opencl_device,
        IPLRadeonRaysDevice radeon_rays_device,
        Ref<ResonanceProbeData> probe_data_res,
        Vector3 endpoint_position,
        float influence_radius,
        IPLBakedDataVariation variation,
        const char* error_prefix,
        const char* success_msg,
        int num_bounces = 4,
        int num_rays = 4096,
        void (*progress_callback)(float, void*) = nullptr,
        void* progress_user_data = nullptr,
        int num_threads = 2,
        int ambisonics_order = resonance::kBakeDefaultAmbisonicsOrder);
};

} // namespace godot

#endif
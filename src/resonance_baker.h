#ifndef RESONANCE_BAKER_H
#define RESONANCE_BAKER_H

#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <phonon.h>

#include "resonance_probe_data.h"
#include "resonance_utils.h"

namespace godot {

    class ResonanceBaker {
    public:
        ResonanceBaker();
        ~ResonanceBaker();

        // Generation type for probe placement
        enum ProbeGenType { GEN_CENTROID = 0, GEN_UNIFORM_FLOOR = 1, GEN_VOLUME = 2 };

        // Generate grid points in world space based on volume transform, extents, spacing, and generation type.
        PackedVector3Array generate_manual_grid(
            const Transform3D& volume_transform,
            Vector3 extents,
            float spacing,
            int generation_type,
            float height_above_floor
        );

        /// Bake using Steam Audio iplProbeArrayGenerateProbes + iplProbeBatchAddProbeArray (scene-aware placement).
        /// Only for GEN_CENTROID (0) and GEN_UNIFORM_FLOOR (1). Use bake_manual_grid for GEN_VOLUME (2).
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
            int num_threads = 2
        );

		// Internal method to create grid points in local space, then transform to world space.
		// reflection_type: 0 = Convolution (BAKECONVOLUTION), 1 = Parametric (BAKEPARAMETRIC)
        // progress_callback, progress_user_data: optional; when non-null, progress is reported during bake
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
            int num_threads = 2
        );

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
            int num_threads = 2
        );

        /// Bake reflections with STATICSOURCE variation. Requires probe_data with existing probes (from Bake Probes).
        /// endpoint_position: world position of the static source. influence_radius: probes within this distance get data.
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
            int num_threads = 2
        );

        /// Bake reflections with STATICLISTENER variation. Requires probe_data with existing probes.
        /// endpoint_position: world position of the static listener. influence_radius: probes within this distance get data.
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
            int num_threads = 2
        );
    };

} // namespace godot

#endif
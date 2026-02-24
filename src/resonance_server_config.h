#ifndef RESONANCE_SERVER_CONFIG_H
#define RESONANCE_SERVER_CONFIG_H

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <functional>

#include "resonance_sofa_asset.h"

namespace godot {

/// Bundles all ResonanceServer configuration values and parsing from Dictionary.
/// Used by ResonanceServer as single source of truth for runtime config.
struct ResonanceServerConfig {
    // Audio
    int sample_rate = 48000;
    int frame_size = 512;
    int ambisonic_order = 1;
    float max_reverb_duration = 2.0f;

    // Simulation
    int simulation_threads = 1;
    float simulation_cpu_cores_percent = 0.05f;
    int max_rays = 4096;
    int max_bounces = 4;
    float reverb_influence_radius = 10000.0f;
    float reverb_max_distance = 0.0f;
    float reverb_transmission_amount = 1.0f;

    // Reflection
    int reflection_type = 0;
    float hybrid_reverb_transition_time = 1.0f;
    float hybrid_reverb_overlap_percent = 0.25f;
    int transmission_type = 0;
    int occlusion_type = 1;

    // HRTF
    Ref<ResonanceSOFAAsset> hrtf_sofa_asset;
    bool reverb_binaural = true;
    bool hrtf_interpolation_bilinear = false;
    bool use_virtual_surround = false;

    // Pathing
    bool pathing_enabled = false;
    float pathing_vis_radius = 0.5f;
    float pathing_vis_threshold = 0.1f;
    float pathing_vis_range = 100.0f;
    bool pathing_normalize_eq = true;

    // OpenCL / Radeon Rays
    bool use_radeon_rays = false;
    int opencl_device_type = 0;
    int opencl_device_index = 0;

    // Context
    bool context_validation = false;
    int context_simd_level = -1;

    // Realtime reflection quality
    float realtime_irradiance_min_distance = 0.1f;
    float realtime_simulation_duration = 2.0f;
    int realtime_num_diffuse_samples = 32;

    // Output flags
    bool output_direct_enabled = true;
    bool output_reverb_enabled = true;
    bool debug_occlusion = false;
    bool debug_reflections = false;
    bool debug_pathing = false;

    // Perspective correction
    bool perspective_correction_enabled = false;
    float perspective_correction_factor = 1.0f;

    // Throttling
    int geometry_update_throttle = 4;
    int simulation_tick_throttle = 1;
    float simulation_update_interval = 0.1f;

    /// Apply config from Dictionary. Optional get_bake_pathing_param used for pathing_vis_* fallback when keys missing.
    void apply(const Dictionary& config,
        std::function<float(const char*, float)> get_bake_pathing_param = nullptr);

private:
    static int config_int(const Dictionary& c, const char* key, int def);
    static float config_float(const Dictionary& c, const char* key, float def);
    static bool config_bool(const Dictionary& c, const char* key, bool def);
    static void config_sofa_asset(const Dictionary& c, const char* key, Ref<ResonanceSOFAAsset>& out);
};

} // namespace godot

#endif // RESONANCE_SERVER_CONFIG_H

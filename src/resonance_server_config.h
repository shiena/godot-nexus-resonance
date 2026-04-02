#ifndef RESONANCE_SERVER_CONFIG_H
#define RESONANCE_SERVER_CONFIG_H

#include <functional>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "resonance_constants.h"
#include "resonance_sofa_asset.h"

namespace godot {

/// Bundles all ResonanceServer configuration values and parsing from Dictionary.
/// Used by ResonanceServer as single source of truth for runtime config.
struct ResonanceServerConfig {
    // Audio
    int sample_rate = 48000;
    int frame_size = resonance::kGodotDefaultFrameSize;
    int ambisonic_order = 1;
    float max_reverb_duration = 2.0f;

    // Simulation
    int simulation_threads = 1;
    float simulation_cpu_cores_percent = resonance::kDefaultSimulationCpuCoresPercent;
    int max_rays = 4096;
    int max_bounces = 4;
    float reverb_influence_radius = 10000.0f;
    float reverb_max_distance = 0.0f;
    float reverb_transmission_amount = 1.0f;

    // Reflection
    int reflection_type = 0;
    /// When player reflections_type is Use Global: 0=Baked, 1=Realtime
    int default_reflections_mode = 0;
    float hybrid_reverb_transition_time = 1.0f;
    float hybrid_reverb_overlap_percent = 0.25f;
    int transmission_type = 1;
    int occlusion_type = 1;
    /// IPLSimulationSettings::maxNumOcclusionSamples (cap for volumetric occlusion)
    int max_occlusion_samples = resonance::kMaxOcclusionSamples;
    /// IPLSimulationSettings::maxNumSources (and TAN maxSources)
    int max_simulation_sources = resonance::kMaxSimulationSources;

    // HRTF
    float hrtf_volume_db = 0.0f;
    /// 0=None (IPL_HRTFNORMTYPE_NONE), 1=RMS — applies to embedded default HRTF only; SOFA uses asset norm_type.
    int hrtf_normalization_type = 0;
    Ref<ResonanceSOFAAsset> hrtf_sofa_asset;
    bool reverb_binaural = true;
    bool hrtf_interpolation_bilinear = false;
    bool use_virtual_surround = false;
    /// Direct path non-HRTF panning: 1,2,4,6,8 (Mono/Stereo/Quad/5.1/7.1). Invalid values become stereo.
    int direct_speaker_channels = 2;

    // Pathing
    bool pathing_enabled = false;
    float pathing_vis_radius = 0.5f;
    float pathing_vis_threshold = 0.1f;
    float pathing_vis_range = 100.0f;
    bool pathing_normalize_eq = true;
    /// Runtime pathing: IPL numVisSamples when pathing on (1–16). Independent of bake_pathing_num_samples.
    int pathing_num_vis_samples = resonance::kRuntimePathingDefaultNumVisSamples;
    /// Default when ResonancePlayerConfig uses Use Global (-1) for path validation (dynamic occlusion of baked paths).
    bool path_validation_enabled = true;
    /// Default when player uses Use Global (-1) for find-alternate-paths (requires validation on to take effect).
    bool find_alternate_paths = false;

    // Ray tracer / OpenCL / Radeon Rays
    /// 0=Default (built-in Phonon), 1=Embree, 2=Radeon Rays, 3=Custom (Godot Physics)
    int scene_type = 0;
    /// Godot PhysicsRayQueryParameters3D collision_mask when scene_type==3; -1 = all layers.
    int physics_ray_collision_mask = -1;
    int opencl_device_type = 0; // 0=GPU, 1=CPU, 2=Any
    int opencl_device_index = 0;

    // Context
    /// IPL validation layer: when true, Steam Audio validates API params (can warn on reverbTimes=0, eqCoeffs>1).
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
    /// 0 = run iplSimulatorRunDirect every worker wake (default). >0 = at most once per this many seconds on non-heavy ticks.
    float direct_sim_interval = 0.0f;
    /// When true, ResonanceRuntime flushes pending source updates once per frame; players enqueue instead of try_update_source.
    bool batch_source_updates = true;

    /// Apply config from Dictionary. Optional get_bake_pathing_param used for pathing_vis_* fallback when keys missing.
    /// config_int truncates FLOAT to int; use integer values from GDScript/JSON for exact results.
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

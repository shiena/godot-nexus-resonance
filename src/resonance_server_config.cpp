#include "resonance_server_config.h"
#include "resonance_sofa_asset.h"
#include "resonance_constants.h"
#include <thread>

namespace godot {

int ResonanceServerConfig::config_int(const Dictionary& c, const char* key, int def) {
    if (!c.has(key)) return def;
    Variant v = c[key];
    if (v.get_type() == Variant::INT) return (int)v;
    if (v.get_type() == Variant::FLOAT) return (int)(double)v;
    return def;
}

float ResonanceServerConfig::config_float(const Dictionary& c, const char* key, float def) {
    if (!c.has(key)) return def;
    Variant v = c[key];
    if (v.get_type() == Variant::FLOAT) return (float)v;
    if (v.get_type() == Variant::INT) return (float)(int)v;
    return def;
}

bool ResonanceServerConfig::config_bool(const Dictionary& c, const char* key, bool def) {
    if (!c.has(key)) return def;
    Variant v = c[key];
    if (v.get_type() == Variant::BOOL) return (bool)v;
    return def;
}

void ResonanceServerConfig::config_sofa_asset(const Dictionary& c, const char* key, Ref<ResonanceSOFAAsset>& out) {
    if (!c.has(key)) { out.unref(); return; }
    Variant v = c[key];
    if (v.get_type() != Variant::OBJECT) { out.unref(); return; }
    Object* o = v.operator Object*();
    if (!o) { out.unref(); return; }
    ResonanceSOFAAsset* a = Object::cast_to<ResonanceSOFAAsset>(o);
    if (a && a->is_valid())
        out = Ref<ResonanceSOFAAsset>(a);
    else
        out.unref();
}

void ResonanceServerConfig::apply(const Dictionary& config,
    std::function<float(const char*, float)> get_bake_pathing_param) {
    sample_rate = config_int(config, "sample_rate", sample_rate);
    frame_size = config_int(config, "audio_frame_size", frame_size);
    if (frame_size != 256 && frame_size != 512 && frame_size != 1024)
        frame_size = resonance::kGodotDefaultFrameSize;
    ambisonic_order = config_int(config, "ambisonic_order", ambisonic_order);
    max_reverb_duration = config_float(config, "max_reverb_duration", max_reverb_duration);
    simulation_cpu_cores_percent = config_float(config, "simulation_cpu_cores_percent", simulation_cpu_cores_percent);
    if (simulation_cpu_cores_percent <= 0.0f || simulation_cpu_cores_percent > 1.0f)
        simulation_cpu_cores_percent = 0.05f;
    unsigned int nc = std::thread::hardware_concurrency();
    if (nc == 0) nc = 1;
    simulation_threads = static_cast<int>(std::max(1u, static_cast<unsigned int>(simulation_cpu_cores_percent * static_cast<float>(nc))));
    max_rays = config_int(config, "realtime_rays", max_rays);
    max_bounces = config_int(config, "realtime_bounces", max_bounces);
    reverb_influence_radius = config_float(config, "reverb_influence_radius", reverb_influence_radius);
    reverb_max_distance = config_float(config, "reverb_max_distance", reverb_max_distance);
    reverb_transmission_amount = config_float(config, "reverb_transmission_amount", reverb_transmission_amount);
    if (reverb_transmission_amount < 0.0f) reverb_transmission_amount = 0.0f;
    if (reverb_transmission_amount > 1.0f) reverb_transmission_amount = 1.0f;
    reflection_type = config_int(config, "reflection_type", reflection_type);
    if (reflection_type < 0) reflection_type = 0;
    if (reflection_type > 3) reflection_type = 3;
    hybrid_reverb_transition_time = config_float(config, "hybrid_reverb_transition_time", hybrid_reverb_transition_time);
    if (hybrid_reverb_transition_time < 0.1f) hybrid_reverb_transition_time = 0.1f;
    if (hybrid_reverb_transition_time > 2.0f) hybrid_reverb_transition_time = 2.0f;
    float overlap_pct = config_float(config, "hybrid_reverb_overlap_percent", hybrid_reverb_overlap_percent * 100.0f);
    hybrid_reverb_overlap_percent = (overlap_pct >= 0.0f && overlap_pct <= 100.0f) ? (overlap_pct / 100.0f) : 0.25f;
    transmission_type = config_int(config, "transmission_type", transmission_type);
    occlusion_type = config_int(config, "occlusion_type", occlusion_type);
    config_sofa_asset(config, "hrtf_sofa_asset", hrtf_sofa_asset);
    reverb_binaural = config_bool(config, "reverb_binaural", reverb_binaural);
    use_virtual_surround = config_bool(config, "use_virtual_surround", use_virtual_surround);
    hrtf_interpolation_bilinear = config_bool(config, "hrtf_interpolation_bilinear", hrtf_interpolation_bilinear);
    pathing_enabled = config_bool(config, "pathing_enabled", pathing_enabled);

    if (config.has("pathing_vis_radius"))
        pathing_vis_radius = config_float(config, "pathing_vis_radius", pathing_vis_radius);
    else if (get_bake_pathing_param)
        pathing_vis_radius = get_bake_pathing_param("bake_pathing_radius", resonance::kBakePathingDefaultRadius);
    if (pathing_vis_radius < 0.0f) pathing_vis_radius = 0.0f;
    if (pathing_vis_radius > 2.0f) pathing_vis_radius = 2.0f;

    if (config.has("pathing_vis_threshold"))
        pathing_vis_threshold = config_float(config, "pathing_vis_threshold", pathing_vis_threshold);
    else if (get_bake_pathing_param)
        pathing_vis_threshold = get_bake_pathing_param("bake_pathing_threshold", resonance::kBakePathingDefaultThreshold);
    if (pathing_vis_threshold < 0.0f) pathing_vis_threshold = 0.0f;
    if (pathing_vis_threshold > 1.0f) pathing_vis_threshold = 1.0f;

    if (config.has("pathing_vis_range"))
        pathing_vis_range = config_float(config, "pathing_vis_range", pathing_vis_range);
    else if (get_bake_pathing_param)
        pathing_vis_range = get_bake_pathing_param("bake_pathing_vis_range", resonance::kBakePathingDefaultVisRange);
    if (pathing_vis_range < 1.0f) pathing_vis_range = 1.0f;
    if (pathing_vis_range > 1000.0f) pathing_vis_range = 1000.0f;

    pathing_normalize_eq = config_bool(config, "pathing_normalize_eq", pathing_normalize_eq);
    use_radeon_rays = config_bool(config, "use_radeon_rays", use_radeon_rays);
    opencl_device_type = config_int(config, "opencl_device_type", opencl_device_type);
    if (opencl_device_type < 0) opencl_device_type = 0;
    if (opencl_device_type > 2) opencl_device_type = 2;
    opencl_device_index = config_int(config, "opencl_device_index", opencl_device_index);
    if (opencl_device_index < 0) opencl_device_index = 0;
    context_validation = config_bool(config, "context_validation", context_validation);
    context_simd_level = config_int(config, "context_simd_level", context_simd_level);
    realtime_irradiance_min_distance = config_float(config, "realtime_irradiance_min_distance", realtime_irradiance_min_distance);
    if (realtime_irradiance_min_distance < 0.05f) realtime_irradiance_min_distance = 0.05f;
    if (realtime_irradiance_min_distance > 0.5f) realtime_irradiance_min_distance = 0.5f;
    realtime_simulation_duration = config_float(config, "realtime_simulation_duration", realtime_simulation_duration);
    if (realtime_simulation_duration < 0.5f) realtime_simulation_duration = 0.5f;
    if (realtime_simulation_duration > 4.0f) realtime_simulation_duration = 4.0f;
    realtime_num_diffuse_samples = config_int(config, "realtime_num_diffuse_samples", realtime_num_diffuse_samples);
    if (realtime_num_diffuse_samples < 8) realtime_num_diffuse_samples = 8;
    if (realtime_num_diffuse_samples > 64) realtime_num_diffuse_samples = 64;
    debug_occlusion = config_bool(config, "debug_occlusion", debug_occlusion);
    debug_reflections = config_bool(config, "debug_reflections", debug_reflections);
    debug_pathing = config_bool(config, "debug_pathing", debug_pathing);
    output_direct_enabled = config_bool(config, "output_direct", output_direct_enabled);
    output_reverb_enabled = config_bool(config, "output_reverb", output_reverb_enabled);
    perspective_correction_enabled = config_bool(config, "perspective_correction_enabled", perspective_correction_enabled);
    perspective_correction_factor = config_float(config, "perspective_correction_factor", perspective_correction_factor);
    geometry_update_throttle = config_int(config, "geometry_update_throttle", geometry_update_throttle);
    if (geometry_update_throttle < 1) geometry_update_throttle = 1;
    if (geometry_update_throttle > 16) geometry_update_throttle = 16;
    simulation_tick_throttle = config_int(config, "simulation_tick_throttle", simulation_tick_throttle);
    if (simulation_tick_throttle < 1) simulation_tick_throttle = 1;
    if (simulation_tick_throttle > 8) simulation_tick_throttle = 8;
    simulation_update_interval = config_float(config, "simulation_update_interval", simulation_update_interval);
    if (simulation_update_interval < 0.0f) simulation_update_interval = 0.0f;
    if (simulation_update_interval > 1.0f) simulation_update_interval = 1.0f;
}

} // namespace godot

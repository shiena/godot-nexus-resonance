#ifndef RESONANCE_STEAM_AUDIO_CONTEXT_H
#define RESONANCE_STEAM_AUDIO_CONTEXT_H

#include <phonon.h>
#include "resonance_sofa_asset.h"

namespace godot {

/// Configuration subset required for Steam Audio context and device initialization.
struct ResonanceSteamAudioContextConfig {
    int sample_rate = 48000;
    int frame_size = 512;
    int ambisonic_order = 1;
    float max_reverb_duration = 2.0f;
    int reflection_type = 0;  // May be modified (e.g. TAN fallback to 0)
    bool use_radeon_rays = false;
    int opencl_device_type = 0;
    int opencl_device_index = 0;
    bool context_validation = false;
    int context_simd_level = -1;
    Ref<ResonanceSOFAAsset> hrtf_sofa_asset;
};

/// Manages Steam Audio context and all device handles (Context, Embree, OpenCL, Radeon Rays, TAN, HRTF).
/// ResonanceServer delegates device creation and shutdown to this class.
class ResonanceSteamAudioContext {
public:
    ResonanceSteamAudioContext() = default;
    ~ResonanceSteamAudioContext();

    /// Initialize context and devices. reflection_type in config may be modified (TAN fallback).
    /// Returns true on success.
    bool init(ResonanceSteamAudioContextConfig& config);

    void shutdown();

    IPLContext get_context() const { return context_; }
    IPLEmbreeDevice get_embree_device() const { return embree_device_; }
    IPLOpenCLDevice get_opencl_device() const { return opencl_device_; }
    IPLRadeonRaysDevice get_radeon_rays_device() const { return radeon_rays_device_; }
    IPLTrueAudioNextDevice get_tan_device() const { return tan_device_; }
    IPLHRTF get_hrtf() const { return hrtf_; }
    IPLSceneType get_scene_type() const { return scene_type_; }

private:
    IPLContext context_ = nullptr;
    IPLEmbreeDevice embree_device_ = nullptr;
    IPLOpenCLDeviceList opencl_device_list_ = nullptr;
    IPLOpenCLDevice opencl_device_ = nullptr;
    IPLRadeonRaysDevice radeon_rays_device_ = nullptr;
    IPLTrueAudioNextDevice tan_device_ = nullptr;
    IPLHRTF hrtf_ = nullptr;
    IPLSceneType scene_type_ = IPL_SCENETYPE_EMBREE;
};

} // namespace godot

#endif // RESONANCE_STEAM_AUDIO_CONTEXT_H

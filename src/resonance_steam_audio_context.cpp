#include "resonance_steam_audio_context.h"
#include "resonance_constants.h"
#include <godot_cpp/variant/utility_functions.hpp>
#if defined(_WIN32) && defined(_MSC_VER)
#include <excpt.h>
#endif

namespace godot {

namespace {
void IPLCALL log_callback(IPLLogLevel level, const char* message) {
    String msg = "SteamAudio: " + String(message);
    if (level == IPL_LOGLEVEL_ERROR) UtilityFunctions::push_error(msg);
    else if (level == IPL_LOGLEVEL_WARNING) UtilityFunctions::push_warning(msg);
}
} // namespace

ResonanceSteamAudioContext::~ResonanceSteamAudioContext() {
    shutdown();
}

bool ResonanceSteamAudioContext::init(ResonanceSteamAudioContextConfig& config) {
    IPLContextSettings ctxSettings{};
    ctxSettings.version = STEAMAUDIO_VERSION;
    ctxSettings.logCallback = log_callback;
    ctxSettings.allocateCallback = nullptr;
    ctxSettings.freeCallback = nullptr;
    ctxSettings.simdLevel = (config.context_simd_level >= 0 && config.context_simd_level <= 4)
        ? static_cast<IPLSIMDLevel>(4 - config.context_simd_level)
        : IPL_SIMDLEVEL_AVX512;
    ctxSettings.flags = config.context_validation ? static_cast<IPLContextFlags>(IPL_CONTEXTFLAGS_VALIDATION) : static_cast<IPLContextFlags>(0);

    if (iplContextCreate(&ctxSettings, &context_) != IPL_STATUS_SUCCESS) {
        UtilityFunctions::push_error("Nexus Resonance: Failed to create IPL Context!");
        return false;
    }

    IPLAudioSettings audioSettings{ config.sample_rate, config.frame_size };
    IPLHRTFSettings hrtfSettings{};
    if (config.hrtf_sofa_asset.is_valid()) {
        hrtfSettings.type = IPL_HRTFTYPE_SOFA;
        hrtfSettings.sofaData = reinterpret_cast<const IPLuint8*>(config.hrtf_sofa_asset->get_data_ptr());
        hrtfSettings.sofaDataSize = static_cast<int>(config.hrtf_sofa_asset->get_size());
        hrtfSettings.volume = ResonanceSOFAAsset::db_to_gain(config.hrtf_sofa_asset->get_volume_db());
        hrtfSettings.normType = (config.hrtf_sofa_asset->get_norm_type() == ResonanceSOFAAsset::NORM_RMS)
            ? IPL_HRTFNORMTYPE_RMS : IPL_HRTFNORMTYPE_NONE;
    } else {
        hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
        hrtfSettings.volume = 1.0f;
        hrtfSettings.normType = IPL_HRTFNORMTYPE_NONE;
    }
    if (iplHRTFCreate(context_, &audioSettings, &hrtfSettings, &hrtf_) != IPL_STATUS_SUCCESS) {
        UtilityFunctions::push_error("Nexus Resonance: HRTF Init Failed");
    }

    bool needs_opencl = config.use_radeon_rays || (config.reflection_type == 3);
    if (needs_opencl && context_) {
#if defined(_WIN32) && defined(_MSC_VER)
        __try {
#endif
        IPLOpenCLDeviceSettings openclSettings{};
        openclSettings.type = (config.reflection_type == 3) ? IPL_OPENCLDEVICETYPE_GPU
            : (config.opencl_device_type == 0) ? IPL_OPENCLDEVICETYPE_GPU
            : (config.opencl_device_type == 1) ? IPL_OPENCLDEVICETYPE_CPU
            : IPL_OPENCLDEVICETYPE_ANY;
        openclSettings.numCUsToReserve = 0;
        openclSettings.fractionCUsForIRUpdate = 0.5f;
        openclSettings.requiresTAN = (config.reflection_type == 3) ? IPL_TRUE : IPL_FALSE;
        IPLerror opencl_list_status = iplOpenCLDeviceListCreate(context_, &openclSettings, &opencl_device_list_);
        if (opencl_list_status == IPL_STATUS_SUCCESS && opencl_device_list_ && iplOpenCLDeviceListGetNumDevices(opencl_device_list_) > 0) {
            int num_devs = iplOpenCLDeviceListGetNumDevices(opencl_device_list_);
            int dev_idx = (config.opencl_device_index >= 0 && config.opencl_device_index < num_devs) ? config.opencl_device_index : 0;
            IPLerror opencl_dev_status = iplOpenCLDeviceCreate(context_, opencl_device_list_, dev_idx, &opencl_device_);
            if (opencl_dev_status == IPL_STATUS_SUCCESS && opencl_device_) {
                if (config.use_radeon_rays) {
                    IPLRadeonRaysDeviceSettings rrSettings{};
                    IPLerror rr_status = iplRadeonRaysDeviceCreate(opencl_device_, &rrSettings, &radeon_rays_device_);
                    if (rr_status == IPL_STATUS_SUCCESS && radeon_rays_device_) {
                        scene_type_ = IPL_SCENETYPE_RADEONRAYS;
                        UtilityFunctions::print("Nexus Resonance: Using Radeon Rays (GPU) for ray tracing.");
                    } else {
                        iplOpenCLDeviceRelease(&opencl_device_);
                        opencl_device_ = nullptr;
                    }
                }
                if (config.reflection_type == 3 && opencl_device_) {
                    IPLTrueAudioNextDeviceSettings tanSettings{};
                    tanSettings.frameSize = config.frame_size;
                    tanSettings.irSize = (IPLint32)(config.max_reverb_duration * (float)config.sample_rate);
                    tanSettings.order = config.ambisonic_order;
                    tanSettings.maxSources = 32;
                    IPLerror tan_status = iplTrueAudioNextDeviceCreate(opencl_device_, &tanSettings, &tan_device_);
                    if (tan_status != IPL_STATUS_SUCCESS || !tan_device_) {
                        tan_device_ = nullptr;
                        config.reflection_type = 0;
                        UtilityFunctions::push_warning("Nexus Resonance: TrueAudio Next (TAN) init failed. Falling back to Convolution. TAN requires AMD GPU with TrueAudio Next support.");
                    } else {
                        UtilityFunctions::print("Nexus Resonance: Using TrueAudio Next (AMD GPU) for reverb convolution.");
                    }
                }
            }
            if (opencl_device_list_) iplOpenCLDeviceListRelease(&opencl_device_list_);
            opencl_device_list_ = nullptr;
        }
        if (!opencl_device_ && opencl_device_list_) {
            iplOpenCLDeviceListRelease(&opencl_device_list_);
            opencl_device_list_ = nullptr;
        }
        if (config.reflection_type == 3 && !tan_device_) {
            if (opencl_device_) { iplOpenCLDeviceRelease(&opencl_device_); opencl_device_ = nullptr; }
        }
#if defined(_WIN32) && defined(_MSC_VER)
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            opencl_device_list_ = nullptr;
            opencl_device_ = nullptr;
            tan_device_ = nullptr;
            if (config.reflection_type == 3) {
                config.reflection_type = 0;
                UtilityFunctions::push_warning(
                    "Nexus Resonance: TrueAudio Next (TAN) crashed during init (no AMD GPU?). Falling back to Convolution.");
            }
        }
#endif
    }

    if (config.reflection_type == 3 && !tan_device_) {
        config.reflection_type = 0;
        UtilityFunctions::push_warning(
            "Nexus Resonance: TrueAudio Next (TAN) is not available. Falling back to Convolution. "
            "TAN requires an AMD GPU with TrueAudio Next support.");
    }

    if (scene_type_ != IPL_SCENETYPE_RADEONRAYS) {
        IPLEmbreeDeviceSettings embreeSettings{};
        IPLerror embree_status = iplEmbreeDeviceCreate(context_, &embreeSettings, &embree_device_);
        if (embree_status != IPL_STATUS_SUCCESS || !embree_device_) {
            embree_device_ = nullptr;
            scene_type_ = IPL_SCENETYPE_DEFAULT;
            UtilityFunctions::push_warning("Nexus Resonance: Embree unavailable, using built-in ray tracer (IPL_SCENETYPE_DEFAULT).");
        } else {
            scene_type_ = IPL_SCENETYPE_EMBREE;
        }
    }

    return true;
}

void ResonanceSteamAudioContext::shutdown() {
    if (!context_) return;
    if (hrtf_) { iplHRTFRelease(&hrtf_); hrtf_ = nullptr; }
    if (radeon_rays_device_) { iplRadeonRaysDeviceRelease(&radeon_rays_device_); radeon_rays_device_ = nullptr; }
    if (tan_device_) { iplTrueAudioNextDeviceRelease(&tan_device_); tan_device_ = nullptr; }
    if (opencl_device_) { iplOpenCLDeviceRelease(&opencl_device_); opencl_device_ = nullptr; }
    if (opencl_device_list_) { iplOpenCLDeviceListRelease(&opencl_device_list_); opencl_device_list_ = nullptr; }
    if (embree_device_) { iplEmbreeDeviceRelease(&embree_device_); embree_device_ = nullptr; }
    if (context_) {
        iplContextRelease(&context_);
        context_ = nullptr;
    }
}

} // namespace godot

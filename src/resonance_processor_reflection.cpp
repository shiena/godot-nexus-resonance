#include "resonance_processor_reflection.h"
#include "resonance_constants.h"
#include "resonance_log.h"
#include "resonance_math.h"
#include <godot_cpp/core/memory.hpp>
#include <cstdio>
#include <cstring>

namespace godot {

    ResonanceReflectionProcessor::ResonanceReflectionProcessor() {}

    ResonanceReflectionProcessor::~ResonanceReflectionProcessor() {
        cleanup();
    }

    void ResonanceReflectionProcessor::initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order, int p_reflection_type) {
        if (is_initialized) return;

        context = p_context;
        frame_size = p_frame_size;
        sample_rate = p_sample_rate;
        reflection_type = p_reflection_type;

        if (reflection_type == 1) {
            num_channels = 1;
        } else {
            num_channels = (p_ambisonic_order + 1) * (p_ambisonic_order + 1);
        }

        IPLAudioSettings audioSettings{};
        audioSettings.samplingRate = sample_rate;
        audioSettings.frameSize = frame_size;

        IPLReflectionEffectType effectType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
        if (reflection_type == 1) effectType = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
        else if (reflection_type == 2) effectType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
        else if (reflection_type == 3) effectType = IPL_REFLECTIONEFFECTTYPE_TAN;

        IPLReflectionEffectSettings reflSettings{};
        reflSettings.type = effectType;
        reflSettings.irSize = (reflection_type == 1) ? 1 : static_cast<int>(sample_rate * resonance::kDefaultReverbDurationSec);  // TAN uses same as Convolution
        reflSettings.numChannels = num_channels;

        if (iplReflectionEffectCreate(context, &audioSettings, &reflSettings, &reflection_effect) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceReflectionProcessor: iplReflectionEffectCreate failed.");
            return;
        }

        if (iplAudioBufferAllocate(context, 1, frame_size, &sa_mono_buffer) != IPL_STATUS_SUCCESS ||
            iplAudioBufferAllocate(context, num_channels, frame_size, &sa_temp_out_buffer) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceReflectionProcessor: Buffer allocation failed.");
            iplReflectionEffectRelease(&reflection_effect);
            reflection_effect = nullptr;
            return;
        }

        is_initialized = true;
    }

    void ResonanceReflectionProcessor::cleanup() {
        if (reflection_effect) iplReflectionEffectRelease(&reflection_effect);

        if (context) {
            iplAudioBufferFree(context, &sa_mono_buffer);
            iplAudioBufferFree(context, &sa_temp_out_buffer);
        }
        memset(&sa_mono_buffer, 0, sizeof(sa_mono_buffer));
        memset(&sa_temp_out_buffer, 0, sizeof(sa_temp_out_buffer));

        context = nullptr;
        is_initialized = false;
    }

    void ResonanceReflectionProcessor::process_mix(const IPLAudioBuffer& in_buffer,
        const IPLReflectionEffectParams& reverb_params,
        IPLReflectionMixer mixer_handle,
        float reverb_gain) {

        if (!is_initialized || !reflection_effect) return;

        // Downmix (IPL API has non-const param; input is read-only)
        iplAudioBufferDownmix(context, const_cast<IPLAudioBuffer*>(&in_buffer), &sa_mono_buffer);

        // Sanitize reverb_gain to prevent NaN propagation (Steam Audio "invalid IPLfloat32" warning)
        float safe_gain = resonance::sanitize_audio_float(reverb_gain);
        if (safe_gain != 1.0f && sa_mono_buffer.data && sa_mono_buffer.data[0]) {
            for (int i = 0; i < frame_size; i++) {
                sa_mono_buffer.data[0][i] *= safe_gain;
            }
        }
        // Sanitize mono buffer before apply (input may contain NaN from upstream)
        if (sa_mono_buffer.data && sa_mono_buffer.data[0]) {
            for (int i = 0; i < frame_size; i++) {
                sa_mono_buffer.data[0][i] = resonance::sanitize_audio_float(sa_mono_buffer.data[0][i]);
            }
        }

        // Ensure params have valid irSize/numChannels - simulation may leave them 0
        IPLReflectionEffectParams params = reverb_params;
        if (params.irSize <= 0) params.irSize = static_cast<int>(sample_rate * resonance::kDefaultReverbDurationSec);
        if (params.numChannels <= 0) params.numChannels = num_channels;
        for (int i = 0; i < 3; i++) {
            params.reverbTimes[i] = resonance::sanitize_audio_float(params.reverbTimes[i]);
            params.eq[i] = resonance::sanitize_audio_float(params.eq[i]);
        }
        params.delay = resonance::sanitize_audio_float(params.delay);

        // Steam Audio validation requires ir non-null for CONVOLUTION/HYBRID. Skip apply if invalid.
        if ((params.type == IPL_REFLECTIONEFFECTTYPE_CONVOLUTION || params.type == IPL_REFLECTIONEFFECTTYPE_HYBRID) && !params.ir)
            return;

        // Apply (writes to mixer when mixer_handle is non-null, else to sa_temp_out_buffer)
        iplReflectionEffectApply(reflection_effect, &params,
            &sa_mono_buffer, &sa_temp_out_buffer, mixer_handle);
    }

    void ResonanceReflectionProcessor::process_mix_direct(const IPLAudioBuffer& in_buffer,
        const IPLReflectionEffectParams& reverb_params) {
        if (!is_initialized || !reflection_effect) return;

        // IPL API has non-const param; input is read-only
        iplAudioBufferDownmix(context, const_cast<IPLAudioBuffer*>(&in_buffer), &sa_mono_buffer);

        IPLReflectionEffectParams params = reverb_params;
        if (params.irSize <= 0) params.irSize = static_cast<int>(sample_rate * resonance::kDefaultReverbDurationSec);
        if (params.numChannels <= 0) params.numChannels = num_channels;
        for (int i = 0; i < 3; i++) {
            params.reverbTimes[i] = resonance::sanitize_audio_float(params.reverbTimes[i]);
            params.eq[i] = resonance::sanitize_audio_float(params.eq[i]);
        }
        params.delay = resonance::sanitize_audio_float(params.delay);
        if (sa_mono_buffer.data && sa_mono_buffer.data[0]) {
            for (int i = 0; i < frame_size; i++) {
                sa_mono_buffer.data[0][i] = resonance::sanitize_audio_float(sa_mono_buffer.data[0][i]);
            }
        }

        // Steam Audio validation requires ir non-null for CONVOLUTION/HYBRID. Skip apply if invalid.
        if ((params.type == IPL_REFLECTIONEFFECTTYPE_CONVOLUTION || params.type == IPL_REFLECTIONEFFECTTYPE_HYBRID) && !params.ir)
            return;

        // Bypass mixer: output goes directly to sa_temp_out_buffer
        iplReflectionEffectApply(reflection_effect, &params,
            &sa_mono_buffer, &sa_temp_out_buffer, nullptr);
    }

} // namespace godot
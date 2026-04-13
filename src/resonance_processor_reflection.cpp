#include "resonance_processor_reflection.h"
#include "resonance_constants.h"
#include "resonance_log.h"
#include "resonance_math.h"
#include "resonance_utils.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

/// Downmix post-process: optional gain ramp (prev_gain >= 0) or constant gain, then sanitize in one pass where possible.
void preprocess_reflection_mono(float prev_gain, float gain, int frame_size, float* mono) {
    if (!mono || frame_size <= 0)
        return;
    if (prev_gain >= 0.0f) {
        resonance::apply_volume_ramp_and_sanitize(prev_gain, gain, frame_size, mono);
        return;
    }
    const bool scale = std::abs(gain - 1.0f) > 1e-5f;
    for (int i = 0; i < frame_size; i++) {
        float s = mono[i];
        if (scale)
            s *= gain;
        mono[i] = resonance::sanitize_audio_float(s);
    }
}

} // namespace

namespace godot {

ResonanceReflectionProcessor::~ResonanceReflectionProcessor() {
    cleanup();
}

void ResonanceReflectionProcessor::initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order, int p_reflection_type,
                                              float p_max_reverb_duration_sec, int p_convolution_ir_max_samples) {
    if (init_flags != ReflectionInitFlags::NONE)
        return;

    if (!p_context) {
        ResonanceLog::error("ResonanceReflectionProcessor: Context is null.");
        return;
    }

    convolution_ir_max_samples_ = std::max(0, p_convolution_ir_max_samples);

    float dur = resonance::sanitize_audio_float(p_max_reverb_duration_sec);
    if (dur < 0.1f)
        dur = 0.1f;
    if (dur > 10.0f)
        dur = 10.0f;
    effect_ir_duration_sec_ = dur;

    context = p_context;
    frame_size = p_frame_size;
    sample_rate = p_sample_rate;
    reflection_type = p_reflection_type;

    if (reflection_type == resonance::kReflectionParametric) {
        num_channels = 1;
    } else {
        num_channels = (p_ambisonic_order + 1) * (p_ambisonic_order + 1);
    }

    IPLAudioSettings audioSettings{};
    audioSettings.samplingRate = sample_rate;
    audioSettings.frameSize = frame_size;

    IPLReflectionEffectType effectType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
    if (reflection_type == resonance::kReflectionParametric)
        effectType = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    else if (reflection_type == resonance::kReflectionHybrid)
        effectType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
    else if (reflection_type == resonance::kReflectionTan)
        effectType = IPL_REFLECTIONEFFECTTYPE_TAN;

    IPLReflectionEffectSettings reflSettings{};
    reflSettings.type = effectType;
    reflSettings.irSize = (reflection_type == resonance::kReflectionParametric)
                              ? static_cast<IPLint32>(1)
                              : static_cast<IPLint32>(resonance::reverb_ir_size_samples(sample_rate, effect_ir_duration_sec_));
    reflSettings.numChannels = num_channels;

    if (iplReflectionEffectCreate(context, &audioSettings, &reflSettings, &reflection_effect) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceReflectionProcessor: iplReflectionEffectCreate failed.");
        return;
    }
    effect_max_ir_samples_ = static_cast<int>(reflSettings.irSize);
    init_flags = init_flags | ReflectionInitFlags::REFLECTIONEFFECT;

    if (iplAudioBufferAllocate(context, 1, frame_size, &sa_mono_buffer) != IPL_STATUS_SUCCESS ||
        iplAudioBufferAllocate(context, num_channels, frame_size, &sa_temp_out_buffer) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceReflectionProcessor: Buffer allocation failed.");
        iplReflectionEffectRelease(&reflection_effect);
        reflection_effect = nullptr;
        cleanup();
        return;
    }
    init_flags = init_flags | ReflectionInitFlags::BUFFERS;
}

void ResonanceReflectionProcessor::cleanup() {
    if (reflection_effect) {
        iplReflectionEffectRelease(&reflection_effect);
        reflection_effect = nullptr;
    }

    if (context) {
        if (sa_mono_buffer.data)
            iplAudioBufferFree(context, &sa_mono_buffer);
        if (sa_temp_out_buffer.data)
            iplAudioBufferFree(context, &sa_temp_out_buffer);
    }
    memset(&sa_mono_buffer, 0, sizeof(sa_mono_buffer));
    memset(&sa_temp_out_buffer, 0, sizeof(sa_temp_out_buffer));

    context = nullptr;
    init_flags = ReflectionInitFlags::NONE;
    convolution_ir_max_samples_ = 0;
    effect_max_ir_samples_ = 0;
}

void ResonanceReflectionProcessor::process_mix(const IPLAudioBuffer& in_buffer,
                                               const IPLReflectionEffectParams& reverb_params,
                                               IPLReflectionMixer mixer_handle,
                                               float reverb_gain,
                                               float prev_reverb_gain) {

    if (!(init_flags & ReflectionInitFlags::REFLECTIONEFFECT) || !(init_flags & ReflectionInitFlags::BUFFERS) || !reflection_effect)
        return;

    // Downmix (IPL API has non-const param; input is read-only)
    iplAudioBufferDownmix(context, const_cast<IPLAudioBuffer*>(&in_buffer), &sa_mono_buffer);

    float safe_gain = resonance::sanitize_audio_float(reverb_gain);
    float safe_prev = resonance::sanitize_audio_float(prev_reverb_gain);
    if (sa_mono_buffer.data && sa_mono_buffer.data[0])
        preprocess_reflection_mono(safe_prev, safe_gain, frame_size, sa_mono_buffer.data[0]);

    IPLReflectionEffectParams params = reverb_params;
    sanitize_reflection_params(&params);

    // Steam Audio validation requires ir non-null for CONVOLUTION/HYBRID. Skip apply if invalid.
    if ((params.type == IPL_REFLECTIONEFFECTTYPE_CONVOLUTION || params.type == IPL_REFLECTIONEFFECTTYPE_HYBRID) && !params.ir)
        return;

    // Apply (writes to mixer when mixer_handle is non-null, else to sa_temp_out_buffer)
    iplReflectionEffectApply(reflection_effect, &params,
                             &sa_mono_buffer, &sa_temp_out_buffer, mixer_handle);
}

void ResonanceReflectionProcessor::process_mix_direct(const IPLAudioBuffer& in_buffer,
                                                      const IPLReflectionEffectParams& reverb_params,
                                                      float prev_reflections_mix_level,
                                                      float reflections_mix_level) {
    if (!(init_flags & ReflectionInitFlags::REFLECTIONEFFECT) || !(init_flags & ReflectionInitFlags::BUFFERS) || !reflection_effect)
        return;

    // IPL API has non-const param; input is read-only
    iplAudioBufferDownmix(context, const_cast<IPLAudioBuffer*>(&in_buffer), &sa_mono_buffer);

    if (sa_mono_buffer.data && sa_mono_buffer.data[0]) {
        float p = resonance::sanitize_audio_float(prev_reflections_mix_level);
        float c = resonance::sanitize_audio_float(reflections_mix_level);
        resonance::apply_volume_ramp_and_sanitize(p, c, frame_size, sa_mono_buffer.data[0]);
    }

    IPLReflectionEffectParams params = reverb_params;
    sanitize_reflection_params(&params);

    // Steam Audio validation requires ir non-null for CONVOLUTION/HYBRID. Skip apply if invalid.
    if ((params.type == IPL_REFLECTIONEFFECTTYPE_CONVOLUTION || params.type == IPL_REFLECTIONEFFECTTYPE_HYBRID) && !params.ir)
        return;

    // Bypass mixer: output goes directly to sa_temp_out_buffer
    iplReflectionEffectApply(reflection_effect, &params,
                             &sa_mono_buffer, &sa_temp_out_buffer, nullptr);
}

void ResonanceReflectionProcessor::sanitize_reflection_params(IPLReflectionEffectParams* params) const {
    if (!params)
        return;
    if (params->irSize <= 0)
        params->irSize = static_cast<IPLint32>(resonance::reverb_ir_size_samples(sample_rate, effect_ir_duration_sec_));
    if (params->numChannels <= 0)
        params->numChannels = num_channels;
    if (convolution_ir_max_samples_ > 0 && effect_max_ir_samples_ > 0 &&
        (reflection_type == resonance::kReflectionConvolution || reflection_type == resonance::kReflectionHybrid ||
         reflection_type == resonance::kReflectionTan)) {
        const int cap = std::min(convolution_ir_max_samples_, effect_max_ir_samples_);
        if (cap > 0 && params->irSize > cap)
            params->irSize = static_cast<IPLint32>(cap);
    }
    for (int i = 0; i < IPL_NUM_BANDS; i++) {
        params->reverbTimes[i] = resonance::clamp_reverb_time(params->reverbTimes[i]);
        params->eq[i] = resonance::sanitize_audio_float(params->eq[i]);
    }
    params->delay = resonance::sanitize_delay_samples(params->delay);
}

void ResonanceReflectionProcessor::reset_effect() {
    if (reflection_effect)
        iplReflectionEffectReset(reflection_effect);
}

int ResonanceReflectionProcessor::get_tail_size_samples() const {
    if (!(init_flags & ReflectionInitFlags::REFLECTIONEFFECT) || !reflection_effect)
        return 0;
    return iplReflectionEffectGetTailSize(reflection_effect);
}

IPLAudioEffectState ResonanceReflectionProcessor::tail_apply_direct(IPLReflectionEffectParams* params) {
    if (!(init_flags & ReflectionInitFlags::REFLECTIONEFFECT) || !(init_flags & ReflectionInitFlags::BUFFERS) || !reflection_effect || !params)
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    sanitize_reflection_params(params);
    if ((params->type == IPL_REFLECTIONEFFECTTYPE_CONVOLUTION || params->type == IPL_REFLECTIONEFFECTTYPE_HYBRID) && !params->ir)
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    return iplReflectionEffectGetTail(reflection_effect, &sa_temp_out_buffer, nullptr);
}

} // namespace godot

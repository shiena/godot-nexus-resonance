#ifndef RESONANCE_MATH_H
#define RESONANCE_MATH_H

#include <cmath>
#include <cstdint>

namespace resonance {

/// Replace NaN/Inf with 0 to prevent Steam Audio "invalid IPLfloat32" warnings.
inline float sanitize_audio_float(float v) {
    return std::isfinite(v) ? v : 0.0f;
}

/// Sanitize Steam Audio delay (IPLint32): finite float round-trip, NaN/Inf -> 0 samples.
inline int32_t sanitize_delay_samples(int32_t v) {
    float f = static_cast<float>(v);
    f = sanitize_audio_float(f);
    return static_cast<int32_t>(std::lroundf(f));
}

/// Impulse-response length in samples from sample rate and duration (seconds).
inline int32_t reverb_ir_size_samples(int sample_rate, float duration_sec) {
    float d = sanitize_audio_float(duration_sec);
    return static_cast<int32_t>(std::lroundf(static_cast<float>(sample_rate) * d));
}

/// Clamp reverb time to valid range for Steam Audio (PARAMETRIC/HYBRID require > 0).
inline float clamp_reverb_time(float v) {
    float s = sanitize_audio_float(v);
    return (s > 0.1f) ? s : 0.1f;
}

/// Pure C++ volume ramping (no Godot/Phonon dependency).
/// Smoothly interpolates volume to prevent clicks/pops when parameters change.
inline void apply_volume_ramp(float start_vol, float end_vol, int num_samples, float* buffer) {
    if (num_samples == 0 || !buffer)
        return;

    // Optimization: Constant volume
    if (std::abs(start_vol - end_vol) < 1e-5f) {
        if (std::abs(start_vol - 1.0f) > 1e-5f) {
            for (int i = 0; i < num_samples; ++i)
                buffer[i] *= start_vol;
        }
        return;
    }

    float step = (end_vol - start_vol) / (float)num_samples;
    float current = start_vol;
    for (int i = 0; i < num_samples; ++i) {
        buffer[i] *= current;
        current += step;
    }
}

/// Like apply_volume_ramp, then sanitize_audio_float per sample (single pass over the buffer).
inline void apply_volume_ramp_and_sanitize(float start_vol, float end_vol, int num_samples, float* buffer) {
    if (num_samples == 0 || !buffer)
        return;

    if (std::abs(start_vol - end_vol) < 1e-5f) {
        if (std::abs(start_vol - 1.0f) > 1e-5f) {
            for (int i = 0; i < num_samples; ++i) {
                buffer[i] = sanitize_audio_float(buffer[i] * start_vol);
            }
        } else {
            for (int i = 0; i < num_samples; ++i) {
                buffer[i] = sanitize_audio_float(buffer[i]);
            }
        }
        return;
    }

    float step = (end_vol - start_vol) / (float)num_samples;
    float current = start_vol;
    for (int i = 0; i < num_samples; ++i) {
        buffer[i] = sanitize_audio_float(buffer[i] * current);
        current += step;
    }
}

} // namespace resonance

#endif // RESONANCE_MATH_H

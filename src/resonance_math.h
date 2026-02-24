#ifndef RESONANCE_MATH_H
#define RESONANCE_MATH_H

#include <cmath>

namespace resonance {

/// Replace NaN/Inf with 0 to prevent Steam Audio "invalid IPLfloat32" warnings.
inline float sanitize_audio_float(float v) {
    return std::isfinite(v) ? v : 0.0f;
}

/// Pure C++ volume ramping (no Godot/Phonon dependency).
/// Smoothly interpolates volume to prevent clicks/pops when parameters change.
inline void apply_volume_ramp(float start_vol, float end_vol, int num_samples, float* buffer) {
	if (num_samples == 0) return;

	// Optimization: Constant volume
	if (std::abs(start_vol - end_vol) < 1e-5f) {
		if (std::abs(start_vol - 1.0f) > 1e-5f) {
			for (int i = 0; i < num_samples; ++i) buffer[i] *= start_vol;
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

} // namespace resonance

#endif // RESONANCE_MATH_H

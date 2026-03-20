#include "resonance_server.h"
#include "resonance_constants.h"
#include "resonance_math.h"
#include "resonance_utils.h"
#include <cstring>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

OcclusionData ResonanceServer::get_source_occlusion_data(int32_t handle) {
    OcclusionData result;
    result.occlusion = 0.0f;
    result.transmission[0] = 1.0f;
    result.transmission[1] = 1.0f;
    result.transmission[2] = 1.0f;
    result.air_absorption[0] = 1.0f;
    result.air_absorption[1] = 1.0f;
    result.air_absorption[2] = 1.0f;
    result.directivity = 1.0f;
    result.distance_attenuation = 1.0f;
    if (handle < 0 || !_ctx())
        return result;

    std::lock_guard<std::mutex> lock(simulation_mutex);
    IPLSource src = source_manager.get_source(handle);
    if (!src)
        return result;

    IPLSimulationOutputs outputs{};
    iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
    result.occlusion = outputs.direct.occlusion;
    result.transmission[0] = outputs.direct.transmission[0];
    result.transmission[1] = outputs.direct.transmission[1];
    result.transmission[2] = outputs.direct.transmission[2];
    result.air_absorption[0] = outputs.direct.airAbsorption[0];
    result.air_absorption[1] = outputs.direct.airAbsorption[1];
    result.air_absorption[2] = outputs.direct.airAbsorption[2];
    result.directivity = outputs.direct.directivity;
    result.distance_attenuation = outputs.direct.distanceAttenuation;
    iplSourceRelease(&src);
    return result;
}

bool ResonanceServer::fetch_reverb_params(int32_t handle, IPLReflectionEffectParams& out_params) {
    if (handle < 0 || !_ctx())
        return false;

    // Steam Audio can return non-zero reverb times even with no probes and numRays=0 (e.g. scene-based
    // estimate or internal default). For reliable output: only treat as valid when we have a real
    // data source (probe batches for baked, or realtime rays).
    if (_uses_parametric_or_hybrid() || reflection_type == resonance::kReflectionTan) {
        if (max_rays == 0) {
            if (!probe_batch_registry_.has_any_batches())
                return false;
        }
    }

    // For Parametric/Hybrid: avoid iplSourceGetOutputs until RunReflections has run at least once.
    // Before that, simulation_data initializes reverbTimes=0.0f; Steam Audio validation requires >0 for PARAMETRIC.
    if (_uses_parametric_or_hybrid() && !reflections_have_run_once_.load(std::memory_order_acquire))
        return false;
    // Per-source: skip getOutputs(REFLECTIONS) until this source has been through at least one RunReflections.
    // New sources get reverbTimes=0 until the next run; calling getOutputs would trigger Steam Audio validation warning.
    if (_uses_parametric_or_hybrid()) {
        std::lock_guard<std::mutex> lock(reflections_pending_mutex_);
        if (reflections_pending_handles_.count(handle) != 0)
            return false;
    }

    // Convolution (0), Hybrid (2), TAN (3): use try_lock + cache fallback to avoid blocking audio thread.
    // Parametric (1): try_lock + cache fallback. RAII: unique_lock unlocks on scope exit or exception.
    // get_source only after simulation_mutex is held (avoids TOCTOU vs destroy / handle recycle).
    std::unique_lock<std::mutex> lock(simulation_mutex, std::defer_lock);
    if (lock.try_lock()) {
        IPLSource src = source_manager.get_source(handle);
        if (!src)
            return false;
        bool result = false;
        instrumentation_fetch_lock_ok.fetch_add(1, std::memory_order_relaxed);
        IPLSimulationOutputs outputs{};
        if (_uses_parametric_or_hybrid()) {
            outputs.reflections.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
        }
        iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
        bool has_convolution = (outputs.reflections.ir != nullptr);
        bool has_parametric = (outputs.reflections.reverbTimes[0] > 0 || outputs.reflections.reverbTimes[1] > 0 || outputs.reflections.reverbTimes[2] > 0);
        bool has_hybrid = (reflection_type == resonance::kReflectionHybrid && (has_convolution || outputs.reflections.reverbTimes[0] > 0));
        bool has_tan = (reflection_type == resonance::kReflectionTan && outputs.reflections.tanSlot >= 0 && _tan());
        if (has_convolution || (reflection_type == resonance::kReflectionParametric && has_parametric) || has_hybrid || has_tan) {
            out_params = outputs.reflections;
            for (int i = 0; i < resonance::kReverbBandCount; i++) {
                out_params.reverbTimes[i] = resonance::clamp_reverb_time(out_params.reverbTimes[i]);
                out_params.eq[i] = resonance::sanitize_audio_float(out_params.eq[i]);
            }
            out_params.delay = resonance::sanitize_audio_float(out_params.delay);
            // IR validation: reject convolution when ir metadata is invalid (avoids Steam Audio NaN warnings).
            if (has_convolution && out_params.ir != nullptr) {
                const int max_ir_samples = 480000; // ~10 s at 48 kHz
                const int max_ir_channels = 64;    // 7th-order ambisonics
                if (out_params.irSize <= 0 || out_params.irSize > max_ir_samples ||
                    out_params.numChannels <= 0 || out_params.numChannels > max_ir_channels) {
                    out_params.ir = nullptr;
                    has_convolution = false;
                    has_hybrid = (reflection_type == resonance::kReflectionHybrid && outputs.reflections.reverbTimes[0] > 0);
                }
            }
            // For Hybrid: Steam Audio validation requires ir non-null when type=HYBRID. Use PARAMETRIC when no IR.
            bool use_hybrid_type = (reflection_type == resonance::kReflectionHybrid && has_convolution && has_parametric);
            out_params.type = (reflection_type == resonance::kReflectionParametric) ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC : (reflection_type == resonance::kReflectionHybrid && use_hybrid_type) ? IPL_REFLECTIONEFFECTTYPE_HYBRID
                                                                                                                        : (reflection_type == resonance::kReflectionHybrid)                      ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC
                                                                                                                        : (reflection_type == resonance::kReflectionTan)                         ? IPL_REFLECTIONEFFECTTYPE_TAN
                                                                                                                                                                                                 : IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
            if (reflection_type == resonance::kReflectionTan)
                out_params.tanDevice = _tan();
            result = true;
            if (reflection_type == resonance::kReflectionConvolution && has_convolution) {
                reverb_convolution_valid_fetches.fetch_add(1, std::memory_order_relaxed);
            }
            if (reflection_type == resonance::kReflectionParametric && has_parametric) {
                std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
                for (int i = 0; i < resonance::kReverbBandCount; i++) {
                    reverb_param_cache_write_[handle].reverbTimes[i] = resonance::clamp_reverb_time(outputs.reflections.reverbTimes[i]);
                    reverb_param_cache_write_[handle].eq[i] = outputs.reflections.eq[i];
                }
                reverb_param_cache_write_[handle].valid = true;
                reverb_cache_dirty_.store(true);
            }
            if (_uses_convolution_or_hybrid_or_tan() && (has_convolution || has_hybrid || has_tan)) {
                std::lock_guard<std::mutex> c_lock(reflection_cache_mutex_);
                reflection_param_cache_write_[handle].params = out_params;
                reflection_param_cache_write_[handle].valid = true;
                reflection_cache_dirty_.store(true);
            }
        }
        iplSourceRelease(&src);
        return result;
    }
    bool result = false;
    if (reflection_type == resonance::kReflectionParametric) {
        if (reverb_cache_dirty_.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
            reverb_param_cache_read_.swap(reverb_param_cache_write_);
        }
        auto it = reverb_param_cache_read_.find(handle);
        if (it != reverb_param_cache_read_.end() && it->second.valid) {
            memset(&out_params, 0, sizeof(out_params));
            out_params.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
            for (int i = 0; i < resonance::kReverbBandCount; i++) {
                out_params.reverbTimes[i] = resonance::clamp_reverb_time(it->second.reverbTimes[i]);
                out_params.eq[i] = resonance::sanitize_audio_float(it->second.eq[i]);
            }
            result = true;
            instrumentation_fetch_cache_hit.fetch_add(1, std::memory_order_relaxed);
        } else {
            instrumentation_fetch_cache_miss.fetch_add(1, std::memory_order_relaxed);
        }
    } else if (_uses_convolution_or_hybrid_or_tan()) {
        if (reflection_cache_dirty_.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> c_lock(reflection_cache_mutex_);
            reflection_param_cache_read_.swap(reflection_param_cache_write_);
        }
        auto it = reflection_param_cache_read_.find(handle);
        if (it != reflection_param_cache_read_.end() && it->second.valid) {
            out_params = it->second.params;
            result = true;
            instrumentation_fetch_cache_hit.fetch_add(1, std::memory_order_relaxed);
        } else {
            instrumentation_fetch_cache_miss.fetch_add(1, std::memory_order_relaxed);
        }
    }

    return result;
}

bool ResonanceServer::fetch_pathing_params(int32_t handle, IPLPathEffectParams& out_params) {
    if (handle < 0 || !_ctx() || !pathing_enabled)
        return false;

    // Use try_lock + cache fallback to avoid blocking audio thread (same pattern as fetch_reverb_params).
    // get_source only after simulation_mutex is held (avoids TOCTOU vs destroy / handle recycle).
    std::unique_lock<std::mutex> lock(simulation_mutex, std::defer_lock);
    if (lock.try_lock()) {
        IPLSource src = source_manager.get_source(handle);
        if (!src)
            return false;
        bool result = false;
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_PATHING, &outputs);
        if (outputs.pathing.shCoeffs != nullptr) {
            int order = outputs.pathing.order;
            int sh_count = (order >= 0) ? (order + 1) * (order + 1) : 0;
            if (sh_count > 0 && outputs.pathing.shCoeffs) {
                // Copy into stable pathing_param_output_ so out_params.shCoeffs stays valid after unlock.
                std::lock_guard<std::mutex> c_lock(pathing_cache_mutex_);
                CachedPathingParams& out_entry = pathing_param_output_[handle];
                out_entry.eqCoeffs[0] = outputs.pathing.eqCoeffs[0];
                out_entry.eqCoeffs[1] = outputs.pathing.eqCoeffs[1];
                out_entry.eqCoeffs[2] = outputs.pathing.eqCoeffs[2];
                out_entry.sh_coeffs.assign(outputs.pathing.shCoeffs, outputs.pathing.shCoeffs + sh_count);
                out_entry.order = order;
                out_entry.valid = true;
                out_params = outputs.pathing;
                out_params.shCoeffs = out_entry.sh_coeffs.data();

                // Cache for lock-miss fallback when audio thread misses lock next time.
                CachedPathingParams& entry = pathing_param_cache_write_[handle];
                entry.eqCoeffs[0] = outputs.pathing.eqCoeffs[0];
                entry.eqCoeffs[1] = outputs.pathing.eqCoeffs[1];
                entry.eqCoeffs[2] = outputs.pathing.eqCoeffs[2];
                entry.sh_coeffs = out_entry.sh_coeffs;
                entry.order = order;
                entry.valid = true;
                pathing_cache_dirty_.store(true);
                out_params.binaural = reverb_binaural ? IPL_TRUE : IPL_FALSE;
                out_params.hrtf = _hrtf();
                out_params.normalizeEQ = pathing_normalize_eq ? IPL_TRUE : IPL_FALSE;
                result = true;
            } else {
                // sh_count <= 0: shCoeffs would point to internal Steam buffer, which RunPathing overwrites after unlock.
                // Do not return result = true with unstable pointer; pathing is skipped for this source this frame.
                result = false;
            }
        } else {
            if (!pathing_no_data_warned) {
                pathing_no_data_warned = true;
                UtilityFunctions::push_warning("Nexus Resonance: Pathing enabled but no pathing data. Bake Pathing separately after Bake Probes (Editor: select ProbeVolume -> Bake Pathing).");
            }
        }
        iplSourceRelease(&src);
        return result;
    }
    bool result = false;
    {
        // Lock miss: use cached params if available. Copy into stable output buffer so pointer stays valid.
        std::lock_guard<std::mutex> c_lock(pathing_cache_mutex_);
        if (pathing_cache_dirty_.exchange(false, std::memory_order_acq_rel)) {
            pathing_param_cache_read_.swap(pathing_param_cache_write_);
        }
        auto it = pathing_param_cache_read_.find(handle);
        if (it != pathing_param_cache_read_.end() && it->second.valid && !it->second.sh_coeffs.empty()) {
            // listener is not set here; caller (ResonancePlayer) sets path_params.listener before process().
            CachedPathingParams& out_entry = pathing_param_output_[handle];
            out_entry.eqCoeffs[0] = it->second.eqCoeffs[0];
            out_entry.eqCoeffs[1] = it->second.eqCoeffs[1];
            out_entry.eqCoeffs[2] = it->second.eqCoeffs[2];
            out_entry.sh_coeffs = it->second.sh_coeffs;
            out_entry.order = it->second.order;
            out_entry.valid = true;
            memset(&out_params, 0, sizeof(out_params));
            for (int i = 0; i < resonance::kReverbBandCount; i++)
                out_params.eqCoeffs[i] = out_entry.eqCoeffs[i];
            out_params.shCoeffs = out_entry.sh_coeffs.data();
            out_params.order = out_entry.order;
            out_params.binaural = reverb_binaural ? IPL_TRUE : IPL_FALSE;
            out_params.hrtf = _hrtf();
            out_params.normalizeEQ = pathing_normalize_eq ? IPL_TRUE : IPL_FALSE;
            result = true;
        }
    }
    return result;
}

void ResonanceServer::set_pathing_deviation_callback(IPLDeviationCallback callback, void* userData) {
    std::lock_guard<std::mutex> sim_lock(simulation_mutex);
    std::lock_guard<std::mutex> lock(_pathing_deviation_mutex);
    if (callback) {
        _pathing_deviation_model.type = IPL_DEVIATIONTYPE_CALLBACK;
        _pathing_deviation_model.callback = callback;
        _pathing_deviation_model.userData = userData;
        _pathing_deviation_callback_enabled = true;
    } else {
        _pathing_deviation_model.type = IPL_DEVIATIONTYPE_DEFAULT;
        _pathing_deviation_model.callback = nullptr;
        _pathing_deviation_model.userData = nullptr;
        _pathing_deviation_callback_enabled = false;
    }
}

void ResonanceServer::clear_pathing_deviation_callback() {
    set_pathing_deviation_callback(nullptr, nullptr);
}

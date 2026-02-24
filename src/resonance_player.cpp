#include "resonance_player.h"
#include "resonance_constants.h"
#include "resonance_server.h"
#include "resonance_probe_volume.h"
#include "resonance_math.h"
#include "resonance_utils.h"
#include "resonance_log.h"
#include <cstdio>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <chrono>

using namespace godot;

/// Apply reverb distance falloff: when distance >= rmd gain becomes 0; between half and rmd, linear fade.
static float apply_reverb_distance_falloff(float gain, float distance, float rmd) {
    if (rmd <= 0.0f || distance <= 0.0f) return gain;
    float half = rmd * 0.5f;
    if (distance >= rmd) return 0.0f;
    if (distance > half) return gain * (1.0f - (distance - half) / half);
    return gain;
}

// ============================================================================
// RESONANCE INTERNAL PLAYBACK
// ============================================================================

ResonanceInternalPlayback::ResonanceInternalPlayback() {
    params_next.apply_air_absorption = false;
    params_next.air_absorption[0] = 1.0f;
    params_next.air_absorption[1] = 1.0f;
    params_next.air_absorption[2] = 1.0f;
    params_next.apply_directivity = false;
    params_next.directivity_value = 1.0f;
    params_next.occlusion = 0.0f;
    params_next.transmission[0] = 1.0f; 
    params_next.transmission[1] = 1.0f; 
    params_next.transmission[2] = 1.0f;
    params_next.attenuation = 1.0f;
    params_next.listener_orientation.ahead = { 0,0,-1 };
    params_next.listener_orientation.up = { 0,1,0 };
    params_next.listener_orientation.right = { 1,0,0 };
    params_next.listener_orientation.origin = { 0,0,0 };
    params_current = params_next;

    size_t buffer_cap = 8192;
    input_ring_l.resize(buffer_cap);
    input_ring_r.resize(buffer_cap);
    output_ring_l.resize(buffer_cap);
    output_ring_r.resize(buffer_cap);

    // Temp buffers resized in _lazy_init to match frame_size_ from ResonanceServer
    temp_process_buffer_l.resize(512);
    temp_process_buffer_r.resize(512);

    // Clean struct init
    memset(&sa_in_buffer, 0, sizeof(IPLAudioBuffer));
    memset(&sa_direct_out_buffer, 0, sizeof(IPLAudioBuffer));
    memset(&sa_final_mix_buffer, 0, sizeof(IPLAudioBuffer));

    parametric_path_sh_coeffs[0] = 0.7071f;
    parametric_path_sh_coeffs[1] = parametric_path_sh_coeffs[2] = parametric_path_sh_coeffs[3] = 0.0f;
}

ResonanceInternalPlayback::~ResonanceInternalPlayback() { _cleanup_steam_audio(); }

void ResonanceInternalPlayback::set_base_playback(const Ref<AudioStreamPlayback>& p_playback) { base_playback = p_playback; }

void ResonanceInternalPlayback::update_parameters(const PlaybackParameters& p_params) {
    params_next = p_params;
    params_dirty.store(true, std::memory_order_release);
}

void ResonanceInternalPlayback::_sync_params() {
    if (params_dirty.load(std::memory_order_acquire)) {
        instrumentation_param_sync_count.fetch_add(1, std::memory_order_relaxed);
        params_current = params_next;
        params_dirty.store(false, std::memory_order_release);

        if (params_current.source_handle != current_source_handle) {
            if (local_source) {
                iplSourceRelease(&local_source);
                local_source = nullptr;
            }
            current_source_handle = params_current.source_handle;
            ResonanceServer* srv = ResonanceServer::get_singleton();
            if (srv && current_source_handle >= 0) {
                local_source = srv->get_source_from_handle(current_source_handle);
            }
        }
    }
}

void ResonanceInternalPlayback::_cleanup_steam_audio() {
    if (local_source) { iplSourceRelease(&local_source); local_source = nullptr; }

    direct_processor.cleanup();
    reflection_processor.cleanup();
    path_processor.cleanup();
    mixer_processor.cleanup();

    if (context) {
        if (sa_in_buffer.data) iplAudioBufferFree(context, &sa_in_buffer);
        if (sa_direct_out_buffer.data) iplAudioBufferFree(context, &sa_direct_out_buffer);
        if (sa_path_out_buffer.data) iplAudioBufferFree(context, &sa_path_out_buffer);
        if (sa_final_mix_buffer.data) iplAudioBufferFree(context, &sa_final_mix_buffer);
    }

    // IMPORTANT: Reset structs to 0 to prevent double-free or invalid access
    memset(&sa_in_buffer, 0, sizeof(IPLAudioBuffer));
    memset(&sa_direct_out_buffer, 0, sizeof(IPLAudioBuffer));
    memset(&sa_path_out_buffer, 0, sizeof(IPLAudioBuffer));
    memset(&sa_final_mix_buffer, 0, sizeof(IPLAudioBuffer));

    is_initialized = false;

    input_ring_l.clear();
    input_ring_r.clear();
    output_ring_l.clear();
    output_ring_r.clear();

    prev_direct_weight = 1.0f;
}

void ResonanceInternalPlayback::_lazy_init_steam_audio(int ignored_rate) {
    ResonanceServer* srv = ResonanceServer::get_singleton();
    if (!srv || !srv->is_initialized()) return;

    current_sample_rate = srv->get_sample_rate();
    frame_size_ = srv->get_audio_frame_size();
    context = srv->get_context_handle();
    int order = srv->get_ambisonic_order();
    int refl_type = srv->get_reflection_type();

    temp_process_buffer_l.resize(frame_size_);
    temp_process_buffer_r.resize(frame_size_);

    // 1. Initialize Direct (always create Ambisonics Encode path for runtime switching)
    direct_processor.initialize(context, current_sample_rate, frame_size_, order, true);

    // 2. Initialize Reflection (convolution or parametric)
    reflection_processor.initialize(context, current_sample_rate, frame_size_, order, refl_type);

    // 3. Initialize Path Processor (for pathing simulation)
    path_processor.initialize(context, current_sample_rate, frame_size_, order);
    // 4. Initialize Mixer Processor (for convolution ambisonic decode)
    mixer_processor.initialize(context, current_sample_rate, frame_size_, order);

    // 5. Allocate Buffers
    iplAudioBufferAllocate(context, 2, frame_size_, &sa_in_buffer);
    iplAudioBufferAllocate(context, 2, frame_size_, &sa_direct_out_buffer);
    iplAudioBufferAllocate(context, 2, frame_size_, &sa_path_out_buffer);
    iplAudioBufferAllocate(context, 2, frame_size_, &sa_final_mix_buffer);

    // Safety check allocations
    if (!sa_in_buffer.data || !sa_direct_out_buffer.data || !sa_path_out_buffer.data || !sa_final_mix_buffer.data) {
        ResonanceLog::error("Playback Init Failed: Buffer allocation returned null.");
        _cleanup_steam_audio();
        return;
    }

    is_initialized = true;
    ResonanceLog::info("Playback Initialized.");
}

void ResonanceInternalPlayback::_process_steam_audio_block() {
    auto t0 = std::chrono::steady_clock::now();
    // CRASH PROTECTION: Check buffers before doing anything
    if (!sa_in_buffer.data || !sa_in_buffer.data[0] || !sa_in_buffer.data[1]) {
        return;
    }

    // 1. Read RingBuffer
    input_ring_l.read(temp_process_buffer_l.data(), frame_size_);
    input_ring_r.read(temp_process_buffer_r.data(), frame_size_);

    // 2. Copy to Steam Audio Buffer (CRITICAL POINT)
    memcpy(sa_in_buffer.data[0], temp_process_buffer_l.data(), frame_size_ * sizeof(float));
    memcpy(sa_in_buffer.data[1], temp_process_buffer_r.data(), frame_size_ * sizeof(float));

    if (local_source) {
        // 3. Run Direct Processor
        ResonanceServer* srv = ResonanceServer::get_singleton();
        int trans_type = (srv && srv->is_initialized()) ? srv->get_transmission_type() : 0;
        bool hrtf_bilinear = (srv && srv->is_initialized()) ? srv->get_hrtf_interpolation_bilinear() : false;
        direct_processor.process(
            params_current.use_ambisonics_encode,
            sa_in_buffer, sa_direct_out_buffer,
            params_current.attenuation,
            params_current.occlusion,
            params_current.transmission,
            params_current.air_absorption,
            params_current.apply_air_absorption,
            params_current.directivity_value,
            params_current.apply_directivity,
            params_current.enable_direct,
            params_current.use_binaural,
            trans_type,
            hrtf_bilinear,
            params_current.spatial_blend,
            params_current.listener_orientation,
            ResonanceUtils::to_ipl_vector3(params_current.source_position)
        );
        // 4. Reverb Injection
        // Convolution (0): feed mixer - Reverb Bus reads it. Parametric (1) / Hybrid (2): process_mix_direct, add to our output.
        // Unified reverb_gain: attenuation * transmission * air_absorption (distance, occlusion, air)
        bool reverb_to_player_output = false;  // Only for Parametric/Hybrid
        float reverb_gain = 1.0f;
        if (srv && params_current.enable_reverb) {
            IPLReflectionEffectParams reverb_params{};
            bool has_reverb = srv->fetch_reverb_params(current_source_handle, reverb_params);

            if (has_reverb) {
                int refl_type = srv->get_reflection_type();
                if (refl_type == 2) {
                    if (params_current.reflections_eq[0] != 1.0f || params_current.reflections_eq[1] != 1.0f || params_current.reflections_eq[2] != 1.0f) {
                        reverb_params.eq[0] *= params_current.reflections_eq[0];
                        reverb_params.eq[1] *= params_current.reflections_eq[1];
                        reverb_params.eq[2] *= params_current.reflections_eq[2];
                    }
                    if (params_current.reflections_delay >= 0) {
                        reverb_params.delay = params_current.reflections_delay;
                    }
                }
                reverb_gain = resonance::sanitize_audio_float(params_current.reverb_pathing_attenuation);
                float tx_avg = (params_current.transmission[0] + params_current.transmission[1] + params_current.transmission[2]) / 3.0f;
                float reverb_tx = 1.0f - srv->get_reverb_transmission_amount() * (1.0f - tx_avg);
                reverb_gain *= resonance::sanitize_audio_float(reverb_tx);
                if (params_current.apply_air_absorption) {
                    float air_avg = (params_current.air_absorption[0] + params_current.air_absorption[1] + params_current.air_absorption[2]) / 3.0f;
                    reverb_gain *= resonance::sanitize_audio_float(air_avg);
                }
                reverb_gain = apply_reverb_distance_falloff(reverb_gain, params_current.distance, srv->get_reverb_max_distance());

                // Convolution (0) / TAN (3): Feed mixer only; Reverb Bus reads it.
                // Parametric (1) / Hybrid (2): process_mix_direct and add to player output.
                if (refl_type == 0 || refl_type == 3) {
                    IPLReflectionMixer mixer = srv->get_reflection_mixer_handle();
                    if (mixer) {
                        // Mono RMS of input (downmix: avg of channels) for convolution debug
                        float sum_sq = 0.0f;
                        int nch = sa_in_buffer.numChannels;
                        if (nch > 0 && sa_in_buffer.data) {
                            for (int i = 0; i < frame_size_; i++) {
                                float mono = 0.0f;
                                for (int c = 0; c < nch && sa_in_buffer.data[c]; c++)
                                    mono += sa_in_buffer.data[c][i];
                                mono /= static_cast<float>(nch);
                                sum_sq += mono * mono;
                            }
                        }
                        float input_rms = (frame_size_ > 0) ? std::sqrt(sum_sq / static_cast<float>(frame_size_)) : 0.0f;
                        srv->record_convolution_feed(reverb_params.ir != nullptr, reverb_gain, input_rms);
                        auto lock = srv->scoped_mixer_lock();
                        reflection_processor.process_mix(sa_in_buffer, reverb_params, mixer, reverb_gain);
                        srv->record_mixer_feed();
                    }
                } else {
                    reverb_to_player_output = true;
                    reflection_processor.process_mix_direct(sa_in_buffer, reverb_params);
                }
            }
            else {
                instrumentation_reverb_miss_blocks.fetch_add(1, std::memory_order_relaxed);
                if (++no_reverb_warn_count <= 3 || no_reverb_warn_count == 200) { /* skip log */ }
                if (no_reverb_warn_count > 200) {
                    ResonanceLog::warn("Playback: No Reverb Params found for source! (Is probe baked? Is source in range?)");
                    no_reverb_warn_count = 0;
                }
            }
        }

        // Apply Volume Ramping (Direct Only). Use mix level: enable_direct * direct_mix_level
        float target_direct = (params_current.enable_direct ? 1.0f : 0.0f) * params_current.direct_mix_level;

        // Apply to Direct (Left & Right)
        for (int i = 0; i < 2; i++) {
            resonance::apply_volume_ramp(prev_direct_weight, target_direct, frame_size_, sa_direct_out_buffer.data[i]);
        }
        prev_direct_weight = target_direct;

        // Mix Direct into final buffer
        memcpy(sa_final_mix_buffer.data[0], sa_direct_out_buffer.data[0], frame_size_ * sizeof(float));
        memcpy(sa_final_mix_buffer.data[1], sa_direct_out_buffer.data[1], frame_size_ * sizeof(float));

        // Add Reverb to player output (Parametric/Hybrid only; Convolution feeds mixer, no fallback)
        if (reverb_to_player_output) {
            IPLAudioBuffer* reverb_buf = reflection_processor.get_direct_output_buffer();
            if (reverb_buf && reverb_buf->data) {
                float refl_mix = resonance::sanitize_audio_float(reverb_gain * params_current.reflections_mix_level);
                if (reflection_processor.is_parametric()) {
                    for (int i = 0; i < frame_size_; i++) {
                        float mono = reverb_buf->data[0][i] * refl_mix;
                        sa_final_mix_buffer.data[0][i] += mono;
                        sa_final_mix_buffer.data[1][i] += mono;
                    }
                } else {
                    AudioFrame reverb_frames[resonance::kGodotDefaultFrameSize];
                    memset(reverb_frames, 0, sizeof(reverb_frames));
                    bool decode_ok = mixer_processor.decode_ambisonic_to_stereo(reverb_buf, params_current.listener_orientation, reverb_frames, frame_size_);
                    if (decode_ok) {
                        for (int i = 0; i < frame_size_; i++) {
                            sa_final_mix_buffer.data[0][i] += reverb_frames[i].left * refl_mix;
                            sa_final_mix_buffer.data[1][i] += reverb_frames[i].right * refl_mix;
                        }
                    }
                }
            }
        }

        // Add Pathing (multi-path sound propagation around obstacles).
        // Only when reverb enabled (reflections or pathing mix > 0) - pathing is indirect sound like reverb.
        if (srv && srv->is_pathing_enabled() && params_current.enable_reverb && params_current.pathing_mix_level > 0.0f) {
            IPLPathEffectParams path_params{};
            bool use_pathing = srv->fetch_pathing_params(current_source_handle, path_params);
            if (use_pathing) {
                path_params.listener = params_current.listener_orientation;
                path_processor.process(sa_in_buffer, path_params, sa_path_out_buffer);
                float path_gain = apply_reverb_distance_falloff(
                    params_current.reverb_pathing_attenuation * params_current.pathing_mix_level * params_current.pathing_occ_scale,
                    params_current.distance, srv->get_reverb_max_distance());
                for (int i = 0; i < frame_size_; i++) {
                    sa_final_mix_buffer.data[0][i] += sa_path_out_buffer.data[0][i] * path_gain;
                    sa_final_mix_buffer.data[1][i] += sa_path_out_buffer.data[1][i] * path_gain;
                }
            }
        }
    }
    else {
        // Passthrough (no local_source)
        instrumentation_passthrough_blocks.fetch_add(1, std::memory_order_relaxed);
        memcpy(sa_final_mix_buffer.data[0], sa_in_buffer.data[0], frame_size_ * sizeof(float));
        memcpy(sa_final_mix_buffer.data[1], sa_in_buffer.data[1], frame_size_ * sizeof(float));

        // Reset Ramps if we lost source, so next time it fades in cleanly
        prev_direct_weight = 1.0f;
    }

    // Safety: clamp output to prevent NaN/overflow from processing bugs
    for (int i = 0; i < frame_size_; i++) {
        sa_final_mix_buffer.data[0][i] = std::clamp(sa_final_mix_buffer.data[0][i], -1.0f, 1.0f);
        sa_final_mix_buffer.data[1][i] = std::clamp(sa_final_mix_buffer.data[1][i], -1.0f, 1.0f);
    }

    // Instrumentation: output RMS and silent-block detection
    float sum_sq = 0.0f;
    for (int i = 0; i < frame_size_; i++) {
        float l = sa_final_mix_buffer.data[0][i];
        float r = sa_final_mix_buffer.data[1][i];
        sum_sq += l * l + r * r;
    }
    float rms = std::sqrt(sum_sq / (2 * frame_size_));
    instrumentation_last_output_rms_q8.store((uint32_t)(rms * 256.0f), std::memory_order_relaxed);
    if (local_source && rms < 0.0001f) instrumentation_silent_output_blocks.fetch_add(1, std::memory_order_relaxed);

    // Output to Output RingBuffer
    output_ring_l.write(sa_final_mix_buffer.data[0], frame_size_);
    output_ring_r.write(sa_final_mix_buffer.data[1], frame_size_);

    auto t1 = std::chrono::steady_clock::now();
    uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    uint64_t cur_max = instrumentation_max_block_time_us.load(std::memory_order_relaxed);
    while (us > cur_max && !instrumentation_max_block_time_us.compare_exchange_weak(cur_max, us, std::memory_order_relaxed))
        {}
}

void ResonanceInternalPlayback::get_instrumentation_snapshot(uint64_t& out_input_dropped, uint64_t& out_output_underrun,
    uint64_t& out_output_blocked, uint64_t& out_mix_calls, uint64_t& out_blocks_processed,
    uint64_t& out_passthrough_blocks, uint64_t& out_reverb_miss_blocks, uint64_t& out_max_block_time_us,
    uint64_t& out_late_mix, uint64_t& out_param_syncs, uint64_t& out_zero_input,
    int32_t& out_mix_frames_min, int32_t& out_mix_frames_max,
    uint64_t& out_silent_blocks, float& out_last_rms) const {
    out_input_dropped = instrumentation_input_dropped.load(std::memory_order_relaxed);
    out_output_underrun = instrumentation_output_underrun.load(std::memory_order_relaxed);
    out_output_blocked = instrumentation_output_blocked.load(std::memory_order_relaxed);
    out_mix_calls = instrumentation_mix_call_count.load(std::memory_order_relaxed);
    out_blocks_processed = instrumentation_blocks_processed.load(std::memory_order_relaxed);
    out_passthrough_blocks = instrumentation_passthrough_blocks.load(std::memory_order_relaxed);
    out_reverb_miss_blocks = instrumentation_reverb_miss_blocks.load(std::memory_order_relaxed);
    out_max_block_time_us = instrumentation_max_block_time_us.load(std::memory_order_relaxed);
    out_late_mix = instrumentation_late_mix_count.load(std::memory_order_relaxed);
    out_param_syncs = instrumentation_param_sync_count.load(std::memory_order_relaxed);
    out_zero_input = instrumentation_zero_input_count.load(std::memory_order_relaxed);
    out_mix_frames_min = instrumentation_mix_frames_min.load(std::memory_order_relaxed);
    out_mix_frames_max = instrumentation_mix_frames_max.load(std::memory_order_relaxed);
    out_silent_blocks = instrumentation_silent_output_blocks.load(std::memory_order_relaxed);
    out_last_rms = instrumentation_last_output_rms_q8.load(std::memory_order_relaxed) / 256.0f;
}

void ResonanceInternalPlayback::reset_instrumentation() {
    instrumentation_input_dropped.store(0, std::memory_order_relaxed);
    instrumentation_output_underrun.store(0, std::memory_order_relaxed);
    instrumentation_output_blocked.store(0, std::memory_order_relaxed);
    instrumentation_mix_call_count.store(0, std::memory_order_relaxed);
    instrumentation_blocks_processed.store(0, std::memory_order_relaxed);
    instrumentation_passthrough_blocks.store(0, std::memory_order_relaxed);
    instrumentation_reverb_miss_blocks.store(0, std::memory_order_relaxed);
    instrumentation_max_block_time_us.store(0, std::memory_order_relaxed);
    instrumentation_late_mix_count.store(0, std::memory_order_relaxed);
    instrumentation_param_sync_count.store(0, std::memory_order_relaxed);
    instrumentation_zero_input_count.store(0, std::memory_order_relaxed);
    instrumentation_mix_frames_min.store(999999, std::memory_order_relaxed);
    instrumentation_mix_frames_max.store(0, std::memory_order_relaxed);
    instrumentation_silent_output_blocks.store(0, std::memory_order_relaxed);
}

int32_t ResonanceInternalPlayback::_mix(AudioFrame* buffer, double rate_scale, int32_t frames) {
    if (base_playback.is_null()) return 0;
    auto now = std::chrono::steady_clock::now();
    instrumentation_mix_call_count.fetch_add(1, std::memory_order_relaxed);
    if (instrumentation_mix_call_count.load(std::memory_order_relaxed) > 1) {
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_mix_time_).count();
        if (elapsed_us > 15000) instrumentation_late_mix_count.fetch_add(1, std::memory_order_relaxed);
    }
    last_mix_time_ = now;
    _sync_params();

    PackedVector2Array mixed_frames = base_playback->mix_audio(rate_scale, frames);
    int32_t samples_read = mixed_frames.size();
    if (samples_read == 0) instrumentation_zero_input_count.fetch_add(1, std::memory_order_relaxed);
    else {
        int32_t cur_min = instrumentation_mix_frames_min.load(std::memory_order_relaxed);
        if (samples_read < cur_min) instrumentation_mix_frames_min.store(samples_read, std::memory_order_relaxed);
        int32_t cur_max = instrumentation_mix_frames_max.load(std::memory_order_relaxed);
        if (samples_read > cur_max) instrumentation_mix_frames_max.store(samples_read, std::memory_order_relaxed);
    }

    // When no input: drain direct effect tail and any remaining output for clean fade-out
    if (samples_read == 0) {
        if (!is_initialized) return 0;
        while (output_ring_l.get_available_read() < (size_t)frames) {
            if (!direct_processor.process_tail(sa_direct_out_buffer)) break;
            output_ring_l.write(sa_direct_out_buffer.data[0], frame_size_);
            output_ring_r.write(sa_direct_out_buffer.data[1], frame_size_);
        }
        int available = (int)output_ring_l.get_available_read();
        int to_copy = (frames < available) ? frames : available;
        for (int i = 0; i < to_copy; i++) {
            float l, r;
            output_ring_l.read(&l, 1);
            output_ring_r.read(&r, 1);
            buffer[i].left = l;
            buffer[i].right = r;
        }
        for (int i = to_copy; i < frames; i++) {
            buffer[i].left = 0.0f;
            buffer[i].right = 0.0f;
        }
        return to_copy;
    }

    if (!is_initialized) {
        _lazy_init_steam_audio(0);
        // If init failed (e.g. out of memory or no context), fallback to passthrough
        if (!is_initialized) {
            for (int i = 0; i < samples_read; i++) {
                buffer[i].left = mixed_frames[i].x;
                buffer[i].right = mixed_frames[i].y;
            }
            return samples_read;
        }
    }

    const Vector2* src_ptr = mixed_frames.ptr();

    for (int i = 0; i < samples_read; i++) {
        float l = src_ptr[i].x;
        float r = src_ptr[i].y;

        if (input_ring_l.get_available_write() > 0) {
            input_ring_l.write(&l, 1);
            input_ring_r.write(&r, 1);
        } else {
            instrumentation_input_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    int blocks_processed_this_call = 0;
    while (blocks_processed_this_call < kMaxBlocksPerMixCall && input_ring_l.get_available_read() >= frame_size_) {
        if (output_ring_l.get_available_write() >= frame_size_) {
            _process_steam_audio_block();
            instrumentation_blocks_processed.fetch_add(1, std::memory_order_relaxed);
            blocks_processed_this_call++;
        } else {
            instrumentation_output_blocked.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }

    int samples_to_output = samples_read;
    int available = (int)output_ring_l.get_available_read();
    int valid_copy = (samples_to_output < available) ? samples_to_output : available;
    if (valid_copy < samples_to_output) {
        instrumentation_output_underrun.fetch_add((uint64_t)(samples_to_output - valid_copy), std::memory_order_relaxed);
    }

    for (int i = 0; i < valid_copy; i++) {
        float l, r;
        output_ring_l.read(&l, 1);
        output_ring_r.read(&r, 1);
        buffer[i].left = l;
        buffer[i].right = r;
    }

    for (int i = valid_copy; i < samples_to_output; i++) {
        buffer[i].left = 0.0f;
        buffer[i].right = 0.0f;
    }

    return samples_to_output;
}

void ResonanceInternalPlayback::_start(double from_pos) {
    direct_processor.reset_for_new_playback();
    if (base_playback.is_valid()) base_playback->start(from_pos);
}
void ResonanceInternalPlayback::_stop() {
    if (base_playback.is_valid()) base_playback->stop();
    direct_processor.reset_for_new_playback();
}
bool ResonanceInternalPlayback::_is_playing() const {
    return base_playback.is_valid() && base_playback->is_playing();
}
int ResonanceInternalPlayback::_get_loop_count() const {
    return base_playback.is_valid() ? base_playback->get_loop_count() : 0;
}
void ResonanceInternalPlayback::_seek(double position) {
    if (base_playback.is_valid()) base_playback->seek(position);
}

void ResonanceInternalStream::set_base_stream(const Ref<AudioStream>& p_stream) { base_stream = p_stream; }
Ref<AudioStreamPlayback> ResonanceInternalStream::_instantiate_playback() const {
    Ref<ResonanceInternalPlayback> playback;
    playback.instantiate();
    if (base_stream.is_valid()) playback->set_base_playback(base_stream->instantiate_playback());
    return playback;
}

ResonancePlayer::ResonancePlayer() {}

ResonancePlayer::~ResonancePlayer() {}

float ResonancePlayer::_config_float(const char* key, float default_val) const {
    if (!player_config.is_valid()) return default_val;
    Variant v = player_config->get(StringName(key));
    return (v.get_type() != Variant::NIL) ? (float)v : default_val;
}
int ResonancePlayer::_config_int(const char* key, int default_val) const {
    if (!player_config.is_valid()) return default_val;
    Variant v = player_config->get(StringName(key));
    return (v.get_type() != Variant::NIL) ? (int)v : default_val;
}
bool ResonancePlayer::_config_bool(const char* key, bool default_val) const {
    if (!player_config.is_valid()) return default_val;
    Variant v = player_config->get(StringName(key));
    return (v.get_type() != Variant::NIL) ? (bool)v : default_val;
}
ResonanceInternalPlayback* ResonancePlayer::_get_resonance_playback() {
    Ref<AudioStreamPlayback> pb = get_stream_playback();
    return pb.is_valid() ? Object::cast_to<ResonanceInternalPlayback>(pb.ptr()) : nullptr;
}

Ref<Curve> ResonancePlayer::_config_curve(const char* key, const Ref<Curve>& default_val) const {
    if (!player_config.is_valid()) return default_val;
    Variant v = player_config->get(StringName(key));
    if (v.get_type() == Variant::OBJECT) {
        Ref<Curve> r = v;
        if (r.is_valid()) return r;
    }
    return default_val;
}

void ResonancePlayer::_refresh_config_cache() {
    if (!player_config.is_valid()) return;
    config_cache_.min_distance = _config_float("min_distance", 1.0f);
    config_cache_.max_distance = _config_float("max_distance", 500.0f);
    config_cache_.source_radius = _config_float("source_radius", 1.0f);
    config_cache_.attenuation_mode = _config_int("attenuation_mode", 0);
    config_cache_.attenuation_curve = _config_curve("attenuation_curve", Ref<Curve>());
    config_cache_.air_absorption_enabled = _config_bool("air_absorption_enabled", true);
    config_cache_.air_absorption_input = _config_int("air_absorption_input", 0);
    config_cache_.air_absorption_low = _config_float("air_absorption_low", 1.0f);
    config_cache_.air_absorption_mid = _config_float("air_absorption_mid", 1.0f);
    config_cache_.air_absorption_high = _config_float("air_absorption_high", 1.0f);
    config_cache_.direct_binaural_override = _config_int("direct_binaural_override", -1);
    config_cache_.directivity_enabled = _config_bool("directivity_enabled", false);
    config_cache_.directivity_weight = _config_float("directivity_weight", 0.0f);
    config_cache_.directivity_power = _config_float("directivity_power", 1.0f);
    config_cache_.spatial_blend = _config_float("spatial_blend", 1.0f);
    config_cache_.use_ambisonics_encode = _config_bool("use_ambisonics_encode", false);
    config_cache_.path_validation_enabled = _config_bool("path_validation_enabled", true);
    config_cache_.find_alternate_paths = _config_bool("find_alternate_paths", true);
    config_cache_.occlusion_samples = _config_int("occlusion_samples", 64);
    config_cache_.max_transmission_surfaces = _config_int("max_transmission_surfaces", 32);
    config_cache_.direct_mix_level = _config_float("direct_mix_level", 1.0f);
    config_cache_.reflections_mix_level = _config_float("reflections_mix_level", 1.0f);
    config_cache_.pathing_mix_level = _config_float("pathing_mix_level", 1.0f);
    config_cache_.pathing_occ_scale = _config_float("pathing_occ_scale", 1.0f);
    config_cache_.reflections_eq_low = _config_float("reflections_eq_low", 1.0f);
    config_cache_.reflections_eq_mid = _config_float("reflections_eq_mid", 1.0f);
    config_cache_.reflections_eq_high = _config_float("reflections_eq_high", 1.0f);
    config_cache_.reflections_delay = _config_int("reflections_delay", -1);
    config_cache_.perspective_override = _config_int("perspective_correction_override", -1);
    config_cache_.perspective_factor = _config_float("perspective_factor", 1.0f);
    config_cache_valid_ = true;
}

void ResonancePlayer::_ready() {
    Engine* eng = Engine::get_singleton();
    if (eng && eng->is_editor_hint()) return;
    if (!player_config.is_valid()) {
        // No config: behave as plain AudioStreamPlayer3D
        if (is_autoplay_enabled()) play();
        return;
    }
    add_to_group("resonance_player");
    debug_drawer.initialize(this);
    set_attenuation_model(ATTENUATION_DISABLED);
    _ensure_source_exists();
    _update_stream_setup();
    if (is_autoplay_enabled()) play_stream();
}

void ResonancePlayer::_exit_tree() {
    stop();
    debug_drawer.cleanup();

    if (source_handle >= 0) {
        ResonanceServer* srv = ResonanceServer::get_singleton();
        if (srv && !ResonanceServer::is_shutting_down()) srv->destroy_source_handle(source_handle);
        source_handle = -1;
    }
}

void ResonancePlayer::_process(double delta) {
    Engine* eng = Engine::get_singleton();
    if (eng && eng->is_editor_hint()) return;
    if (!player_config.is_valid() || !is_playing()) return;

    ResonanceServer* srv = ResonanceServer::get_singleton();
    if (!srv || !srv->is_simulating() || source_handle < 0) return;

    if (config_cache_frame_countdown <= 0 || !config_cache_valid_) {
        _refresh_config_cache();
        config_cache_frame_countdown = kConfigCacheRefreshInterval;
    }
    config_cache_frame_countdown--;

    const ConfigCache& c = config_cache_;

    Transform3D gt = get_global_transform();
    Vector3 forward = -gt.basis.get_column(2);
    Vector3 up = gt.basis.get_column(1);
    bool use_sim_attenuation = true;  // Use sim for all modes so pathing uses distance attenuation (Inverse, Linear, Curve via callback)
    if (c.attenuation_mode == ATTENUATION_LINEAR || c.attenuation_mode == ATTENUATION_CUSTOM_CURVE) {
        PackedFloat32Array curve_samples;
        if (c.attenuation_mode == ATTENUATION_LINEAR) {
            curve_samples.resize(64);
            for (int i = 0; i < 64; i++) curve_samples[i] = 1.0f - (float)i / 63.0f;  // Linear falloff
        } else if (c.attenuation_curve.is_valid()) {
            curve_samples.resize(64);
            for (int i = 0; i < 64; i++) {
                float t = (float)i / 63.0f;
                curve_samples[i] = c.attenuation_curve->sample(t);
            }
        } else {
            curve_samples.resize(64);
            for (int i = 0; i < 64; i++) curve_samples[i] = (i == 0) ? 1.0f : 0.0f;
        }
        srv->set_source_attenuation_callback_data(source_handle, c.attenuation_mode, c.min_distance, c.max_distance, curve_samples);
    }
    const int baked_var = 0;  // Reverb only; Static Source/Listener config is in ProbeVolume
    Vector3 baked_center = get_global_position();
    const float baked_radius = 10000.0f;
    int32_t pathing_batch = -1;
    if (!pathing_probe_volume.is_empty()) {
        Node* node = get_node_or_null(pathing_probe_volume);
        ResonanceProbeVolume* pv = Object::cast_to<ResonanceProbeVolume>(node);
        if (pv) pathing_batch = pv->get_probe_batch_handle();
    }
    // Air absorption simulation only when enabled and Simulation mode; User Defined uses config values
    bool sim_air_absorption = c.air_absorption_enabled && (c.air_absorption_input == 0);
    srv->update_source(source_handle, get_global_position(), c.source_radius,
        forward, up, c.directivity_weight, c.directivity_power, sim_air_absorption,
        use_sim_attenuation, c.min_distance,
        c.path_validation_enabled, c.find_alternate_paths,
        c.occlusion_samples, c.max_transmission_surfaces,
        baked_var, baked_center, baked_radius,
        pathing_batch);

    Vector3 listener_pos = Vector3(0, 0, 0);
    IPLCoordinateSpace3 listener_orient{};

    Viewport* vp = get_viewport();
    if (vp && vp->get_camera_3d()) {
        Camera3D* cam = vp->get_camera_3d();
        listener_pos = cam->get_global_position();
        Vector3 forward = -cam->get_global_transform().basis.get_column(2);
        Vector3 up = cam->get_global_transform().basis.get_column(1);
        Vector3 right = cam->get_global_transform().basis.get_column(0);
        listener_orient.origin = { listener_pos.x, listener_pos.y, listener_pos.z };
        listener_orient.ahead = { forward.x, forward.y, forward.z };
        listener_orient.up = { up.x, up.y, up.z };
        listener_orient.right = { right.x, right.y, right.z };
    }

    OcclusionData occ_data = srv->get_source_occlusion_data(source_handle);

    float attenuation = 1.0f;
    float dist = get_global_position().distance_to(listener_pos);

    if (c.attenuation_mode == ATTENUATION_INVERSE) {
        attenuation = occ_data.distance_attenuation;
    }
    else if (c.attenuation_mode == ATTENUATION_LINEAR) {
        if (dist <= c.min_distance) { attenuation = 1.0f; }
        else if (c.max_distance <= c.min_distance) { attenuation = 0.0f; }
        else if (dist >= c.max_distance) { attenuation = 0.0f; }
        else { attenuation = 1.0f - ((dist - c.min_distance) / (c.max_distance - c.min_distance)); }
    }
    else if (c.attenuation_mode == ATTENUATION_CUSTOM_CURVE) {
        if (c.attenuation_curve.is_valid() && c.max_distance > c.min_distance) {
            float t = (dist - c.min_distance) / (c.max_distance - c.min_distance);
            t = CLAMP(t, 0.0f, 1.0f);
            attenuation = c.attenuation_curve->sample(t);
        }
        else if (c.attenuation_curve.is_valid()) {
            attenuation = (dist <= c.min_distance) ? 1.0f : 0.0f;
        }
        else {
            attenuation = (dist >= c.max_distance) ? 0.0f : 1.0f;
        }
    }

    // Reverb/pathing attenuation: curve/linear modes can produce much higher values than inverse (1/distance),
    // causing overdrive. Cap to inverse level so reverb/pathing stay comparable to inverse mode.
    float reverb_pathing_attenuation = attenuation;
    if (c.attenuation_mode == ATTENUATION_LINEAR || c.attenuation_mode == ATTENUATION_CUSTOM_CURVE) {
        float inverse_ref = (dist > 0.0f && c.min_distance > 0.0f)
            ? (1.0f / (dist >= c.min_distance ? dist : c.min_distance))
            : 1.0f;
        if (inverse_ref > 1.0f) inverse_ref = 1.0f;
        if (attenuation > inverse_ref) reverb_pathing_attenuation = inverse_ref;
    }

    // Air absorption: Simulation mode uses occ_data; User Defined uses config low/mid/high
    Vector3 air_abs;
    if (c.air_absorption_enabled && c.air_absorption_input == 1) {
        air_abs.x = CLAMP(c.air_absorption_low, 0.0f, 1.0f);
        air_abs.y = CLAMP(c.air_absorption_mid, 0.0f, 1.0f);
        air_abs.z = CLAMP(c.air_absorption_high, 0.0f, 1.0f);
    } else {
        air_abs = Vector3(occ_data.air_absorption[0], occ_data.air_absorption[1], occ_data.air_absorption[2]);
    }
    float directivity_val = occ_data.directivity;

    // --- FIX: Variables needed for Logic and Debug ---

    // 1. Check if Reverb is available
    IPLReflectionEffectParams ignored_params{};
    bool has_reverb = srv->fetch_reverb_params(source_handle, ignored_params);

    // 2. Enable flags derived from mix levels (0 = muted) AND Server global switch
    bool direct_enabled = (c.direct_mix_level > 0.0f) && (!srv || srv->is_output_direct_enabled());
    bool reverb_enabled = ((c.reflections_mix_level > 0.0f) || (c.pathing_mix_level > 0.0f)) && (!srv || srv->is_output_reverb_enabled());

    // Perspective correction: -1 = use global, 0 = off, 1 = on. When on, use perspective_factor or global factor.
    bool apply_perspective = (c.perspective_override == 1) || (c.perspective_override == -1 && srv->is_perspective_correction_enabled());
    float perspective_factor_val = (c.perspective_override == 1) ? CLAMP(c.perspective_factor, 0.5f, 2.0f) : srv->get_perspective_correction_factor();

    Vector3 effective_source_pos = get_global_position();
    if (apply_perspective && vp && vp->get_camera_3d()) {
        Camera3D* cam = vp->get_camera_3d();
        Transform3D view_xform = cam->get_global_transform().affine_inverse();
        Vector3 view_pos = view_xform.xform(get_global_position());
        Projection proj = cam->get_camera_projection();
        Vector4 clip = proj.xform(Vector4(view_pos.x, view_pos.y, view_pos.z, 1.0f));
        if (clip.w > 0.01f) {
            float ndc_x = clip.x / clip.w;
            float ndc_y = clip.y / clip.w;
            ndc_x = CLAMP(ndc_x, -1.0f, 1.0f);
            ndc_y = CLAMP(ndc_y, -1.0f, 1.0f);
            float factor = perspective_factor_val;
            float sx = ndc_x * factor;
            float sy = ndc_y * factor;
            Vector3 dir_view(sx, sy, -1.0f);
            float len_sq = dir_view.length_squared();
            if (len_sq > 1e-8f) {
                dir_view = dir_view / std::sqrt(len_sq);
                Vector3 dir_world = cam->get_global_transform().basis.xform(dir_view);
                effective_source_pos = listener_pos + dir_world;
            }
        }
    }

    PlaybackParameters new_params;
    new_params.source_handle = source_handle;
    new_params.occlusion = occ_data.occlusion;
    new_params.transmission[0] = occ_data.transmission[0];
    new_params.transmission[1] = occ_data.transmission[1];
    new_params.transmission[2] = occ_data.transmission[2];
    new_params.attenuation = attenuation;
    new_params.reverb_pathing_attenuation = reverb_pathing_attenuation;
    new_params.distance = dist;
    new_params.source_position = effective_source_pos;
    // Direct binaural: -1 = use global (reverb_binaural), 0 = panning, 1 = binaural
    bool eff_use_binaural = true;
    if (c.direct_binaural_override == -1) {
        eff_use_binaural = srv->use_reverb_binaural();
    } else {
        eff_use_binaural = (c.direct_binaural_override == 1);
    }
    new_params.use_binaural = eff_use_binaural;
    new_params.apply_air_absorption = c.air_absorption_enabled;
    new_params.air_absorption[0] = air_abs.x;
    new_params.air_absorption[1] = air_abs.y;
    new_params.air_absorption[2] = air_abs.z;
    new_params.apply_directivity = c.directivity_enabled;
    new_params.directivity_value = directivity_val;
    new_params.listener_orientation = listener_orient;

    new_params.enable_direct = direct_enabled;
    new_params.enable_reverb = reverb_enabled;
    new_params.has_valid_reverb = has_reverb;
    new_params.spatial_blend = c.spatial_blend;
    new_params.use_ambisonics_encode = c.use_ambisonics_encode;
    new_params.direct_mix_level = c.direct_mix_level;
    new_params.reflections_mix_level = c.reflections_mix_level;
    new_params.pathing_mix_level = c.pathing_mix_level;
    new_params.pathing_occ_scale = c.pathing_occ_scale;
    new_params.reflections_eq[0] = c.reflections_eq_low;
    new_params.reflections_eq[1] = c.reflections_eq_mid;
    new_params.reflections_eq[2] = c.reflections_eq_high;
    new_params.reflections_delay = c.reflections_delay;

    ResonanceInternalPlayback* res_pb = _get_resonance_playback();
    if (res_pb) res_pb->update_parameters(new_params);

    // --- DEBUG DRAWING ---
    ResonanceDebugData dbg_data;
    dbg_data.source_pos = get_global_position();
    dbg_data.listener_pos = listener_pos;
    dbg_data.occlusion = occ_data.occlusion;
    dbg_data.transmission[0] = occ_data.transmission[0];
    dbg_data.transmission[1] = occ_data.transmission[1];
    dbg_data.transmission[2] = occ_data.transmission[2];
    dbg_data.attenuation = attenuation;
    dbg_data.distance = dist;
    dbg_data.air_absorption = air_abs;
    dbg_data.directivity_val = directivity_val;
    dbg_data.air_abs_enabled = c.air_absorption_enabled;
    dbg_data.directivity_enabled = c.directivity_enabled;
    dbg_data.has_reverb = has_reverb;

    debug_drawer.process(
        delta,
        dbg_data,
        srv->is_debug_occlusion_enabled(),
        srv->is_debug_reflections_enabled(),
        get_name()
    );
}

void ResonancePlayer::_ensure_source_exists() {
    if (!player_config.is_valid()) return;
    ResonanceServer* srv = ResonanceServer::get_singleton();
    if (srv && srv->is_initialized() && source_handle < 0) {
        float eff_radius = _config_float("source_radius", 1.0f);
        source_handle = srv->create_source_handle(get_global_position(), eff_radius);
    }
}

void ResonancePlayer::_update_stream_setup() {
    if (!player_config.is_valid()) return;
    Ref<AudioStream> cur = get_stream();
    if (cur.is_valid() && cur->get_class() == "ResonanceInternalStream") return;
    if (cur.is_valid()) {
        internal_stream.instantiate();
        internal_stream->set_base_stream(cur);
        set_stream(internal_stream);
    }
}

void ResonancePlayer::play_stream(double from_pos) {
    _update_stream_setup();
    play(from_pos);
}

void ResonancePlayer::set_pathing_probe_volume(const NodePath& p_path) { pathing_probe_volume = p_path; }
NodePath ResonancePlayer::get_pathing_probe_volume() const { return pathing_probe_volume; }
void ResonancePlayer::set_player_config(const Ref<Resource>& p_config) {
    player_config = p_config;
    config_cache_valid_ = false;
}
Ref<Resource> ResonancePlayer::get_player_config() const { return player_config; }

Dictionary ResonancePlayer::get_audio_instrumentation() {
    Dictionary d;
    ResonanceInternalPlayback* res_pb = _get_resonance_playback();
    if (res_pb) {
            uint64_t input_dropped = 0, output_underrun = 0, output_blocked = 0, mix_calls = 0, blocks = 0;
            uint64_t passthrough = 0, reverb_miss = 0, max_block_us = 0;
            uint64_t late_mix = 0, param_syncs = 0, zero_input = 0;
            int32_t mix_frames_min = 999999, mix_frames_max = 0;
            uint64_t silent_blocks = 0;
            float last_rms = 0.0f;
            res_pb->get_instrumentation_snapshot(input_dropped, output_underrun, output_blocked, mix_calls, blocks,
                passthrough, reverb_miss, max_block_us, late_mix, param_syncs, zero_input,
                mix_frames_min, mix_frames_max, silent_blocks, last_rms);
            d["input_dropped"] = (int64_t)input_dropped;
            d["output_underrun"] = (int64_t)output_underrun;
            d["output_blocked"] = (int64_t)output_blocked;
            d["mix_calls"] = (int64_t)mix_calls;
            d["blocks_processed"] = (int64_t)blocks;
            d["passthrough_blocks"] = (int64_t)passthrough;
            d["reverb_miss_blocks"] = (int64_t)reverb_miss;
            d["max_block_time_us"] = (int64_t)max_block_us;
            d["late_mix_count"] = (int64_t)late_mix;
            d["param_sync_count"] = (int64_t)param_syncs;
            d["zero_input_count"] = (int64_t)zero_input;
            d["mix_frames_min"] = (int)mix_frames_min;
            d["mix_frames_max"] = (int)mix_frames_max;
            d["silent_output_blocks"] = (int64_t)silent_blocks;
            d["last_output_rms"] = last_rms;
    }
    return d;
}

void ResonancePlayer::reset_audio_instrumentation() {
    ResonanceInternalPlayback* res_pb = _get_resonance_playback();
    if (res_pb) res_pb->reset_instrumentation();
}

void ResonancePlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("play_stream", "from_position"), &ResonancePlayer::play_stream, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("set_pathing_probe_volume", "p_path"), &ResonancePlayer::set_pathing_probe_volume);
    ClassDB::bind_method(D_METHOD("get_pathing_probe_volume"), &ResonancePlayer::get_pathing_probe_volume);
    ClassDB::bind_method(D_METHOD("set_player_config", "p_config"), &ResonancePlayer::set_player_config);
    ClassDB::bind_method(D_METHOD("get_player_config"), &ResonancePlayer::get_player_config);
    ClassDB::bind_method(D_METHOD("get_audio_instrumentation"), &ResonancePlayer::get_audio_instrumentation);
    ClassDB::bind_method(D_METHOD("reset_audio_instrumentation"), &ResonancePlayer::reset_audio_instrumentation);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_config", PROPERTY_HINT_RESOURCE_TYPE, "ResonancePlayerConfig"), "set_player_config", "get_player_config");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "pathing_probe_volume", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "ResonanceProbeVolume"), "set_pathing_probe_volume", "get_pathing_probe_volume");

    BIND_ENUM_CONSTANT(ATTENUATION_INVERSE);
    BIND_ENUM_CONSTANT(ATTENUATION_LINEAR);
    BIND_ENUM_CONSTANT(ATTENUATION_CUSTOM_CURVE);
}
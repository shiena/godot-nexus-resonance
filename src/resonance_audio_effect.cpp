#include "resonance_audio_effect.h"
#include "resonance_server.h"
#include "resonance_log.h"
#include <cstdio>
#include <algorithm>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// --- INSTANCE ---

ResonanceAudioEffectInstance::ResonanceAudioEffectInstance() {
    // Constructor; logging disabled by default
    // UtilityFunctions::print("AudioEffectInstance Created.");
}

ResonanceAudioEffectInstance::~ResonanceAudioEffectInstance() {
    processor.cleanup();
}

void ResonanceAudioEffectInstance::_process(const void* src_buffer, AudioFrame* dst_buffer, int32_t frame_count) {
    if (++heartbeat_counter > 100) {
        heartbeat_counter = 0;
    }

    ResonanceServer* srv = ResonanceServer::get_singleton();

    // Check shut down flag to avoid crashes on exit
    if (!srv || !srv->is_initialized() || ResonanceServer::is_shutting_down()) {
        return;
    }

	// --- INITIALIZATION ---
    if (!initialized_processor) {
        ResonanceLog::info("AudioEffect: Initializing MixerProcessor with frame size: " + String::num(frame_count));
        processor.initialize(
            srv->get_context_handle(),
            srv->get_sample_rate(),
            frame_count, // Use actual buffer size from Godot!
            srv->get_ambisonic_order()
        );
        initialized_processor = true;
    }

    IPLReflectionMixer mixer = srv->get_reflection_mixer_handle();
    // No mixer for Parametric (1) or Hybrid (2) - per Steam Audio docs mixer cannot be used with those
    if (!mixer) {
        for (int i = 0; i < frame_count; i++) {
            dst_buffer[i].left = 0.0f;
            dst_buffer[i].right = 0.0f;
        }
        srv->update_reverb_effect_instrumentation(true, false, 0, 0.0f);
        return;
    }

    // Clear output; we replace with mixer content (ignore bus input)
    for (int i = 0; i < frame_count; i++) {
        dst_buffer[i].left = 0.0f;
        dst_buffer[i].right = 0.0f;
    }

    // --- LOCKING & PROCESSING (RAII: unlock on scope exit or exception) ---
    auto mixer_lock = srv->scoped_mixer_lock();

    IPLCoordinateSpace3 listener_coords = srv->get_current_listener_coords();

    // Process
    bool success = processor.process_mixer_return(mixer, listener_coords, dst_buffer, frame_count);

    float peak = 0.0f;
    int32_t frames_written = 0;
    if (success) {
        // Reset Mixer after reading
        iplReflectionMixerReset(mixer);
        frames_written = frame_count;

        // --- SAFETY: Output limiter to prevent ear damage from overflow/NaN
        for (int i = 0; i < frame_count; i++) {
            dst_buffer[i].left = std::clamp(dst_buffer[i].left, -1.0f, 1.0f);
            dst_buffer[i].right = std::clamp(dst_buffer[i].right, -1.0f, 1.0f);
            float v = std::max(std::abs(dst_buffer[i].left), std::abs(dst_buffer[i].right));
            if (v > peak) peak = v;
        }

    }

    srv->update_reverb_effect_instrumentation(false, success, frames_written, peak);
}

// --- EFFECT RESOURCE ---

ResonanceAudioEffect::ResonanceAudioEffect() { set_name("Resonance Reverb"); } // for Godot Audio Bus editor
ResonanceAudioEffect::~ResonanceAudioEffect() {}

Ref<AudioEffectInstance> ResonanceAudioEffect::_instantiate() {
    Ref<ResonanceAudioEffectInstance> ins;
    ins.instantiate();
    return ins;
}

void ResonanceAudioEffect::set_gain_db(float p_db) { gain_db = p_db; }
float ResonanceAudioEffect::get_gain_db() const { return gain_db; }

void ResonanceAudioEffect::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_gain_db", "p_db"), &ResonanceAudioEffect::set_gain_db);
    ClassDB::bind_method(D_METHOD("get_gain_db"), &ResonanceAudioEffect::get_gain_db);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gain_db", PROPERTY_HINT_RANGE, "-60,24,0.1,suffix:dB"), "set_gain_db", "get_gain_db");
}
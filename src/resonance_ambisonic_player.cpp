#include "resonance_ambisonic_player.h"
#include "resonance_constants.h"
#include "resonance_log.h"
#include "resonance_server.h"
#include <algorithm>
#include <cstring>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

// ============================================================================
// PLAYBACK
// ============================================================================

ResonanceAmbisonicInternalPlayback::ResonanceAmbisonicInternalPlayback() {
    params_next.listener_orientation.ahead = {0, 0, -1};
    params_next.listener_orientation.up = {0, 1, 0};
    params_next.listener_orientation.right = {1, 0, 0};
    params_next.listener_orientation.origin = {0, 0, 0};
    params_current = params_next;

    // Buffers initialized in set_channel_playbacks when order is known
}

ResonanceAmbisonicInternalPlayback::~ResonanceAmbisonicInternalPlayback() { _cleanup_steam_audio(); }

void ResonanceAmbisonicInternalPlayback::set_channel_playbacks(const Array& playbacks, int p_order) {
    ambisonic_order = CLAMP(p_order, 1, 3);
    int num_channels = (ambisonic_order + 1) * (ambisonic_order + 1);

    channel_playbacks.clear();
    channel_playbacks.reserve(num_channels);
    for (int i = 0; i < playbacks.size() && i < num_channels; i++) {
        Ref<AudioStreamPlayback> pb = playbacks[i];
        channel_playbacks.push_back(pb);
    }

    // Resize buffers
    size_t in_capacity = resonance::kRingBufferCapacity * num_channels;
    input_ring.resize(in_capacity);

    size_t out_capacity = resonance::kRingBufferCapacity;
    output_ring_l.resize(out_capacity);
    output_ring_r.resize(out_capacity);

    temp_interleaved_input.resize(static_cast<size_t>(resonance::kGodotDefaultFrameSize) * static_cast<size_t>(num_channels)); // Resized to frame_size_ in _lazy_init
}

void ResonanceAmbisonicInternalPlayback::update_parameters(const AmbisonicPlaybackParameters& p_params) {
    params_next = p_params;
    params_dirty.store(true, std::memory_order_release);
}

void ResonanceAmbisonicInternalPlayback::_sync_params() {
    if (params_dirty.load(std::memory_order_acquire)) {
        params_current = params_next;
        params_dirty.store(false, std::memory_order_release);
    }
}

void ResonanceAmbisonicInternalPlayback::_cleanup_steam_audio() {
    processor.cleanup();
    if (context && sa_out_buffer.data) {
        iplAudioBufferFree(context, &sa_out_buffer);
    }
    memset(&sa_out_buffer, 0, sizeof(sa_out_buffer));
    context = nullptr;
    is_initialized = false;

    input_ring.clear();
    output_ring_l.clear();
    output_ring_r.clear();
}

void ResonanceAmbisonicInternalPlayback::_lazy_init_steam_audio() {
    ResonanceServer* srv = ResonanceServer::get_singleton();
    if (!srv || !srv->is_initialized())
        return;

    current_sample_rate = srv->get_sample_rate();
    frame_size_ = srv->get_audio_frame_size();
    context = srv->get_context_handle();

    int num_channels = (ambisonic_order + 1) * (ambisonic_order + 1);
    temp_interleaved_input.resize(static_cast<size_t>(frame_size_) * static_cast<size_t>(num_channels));

    processor.initialize(context, current_sample_rate, frame_size_, ambisonic_order, params_current.rotation_enabled);

    // Allocate Output Buffer (Stereo)
    if (iplAudioBufferAllocate(context, 2, frame_size_, &sa_out_buffer) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceAmbisonicPlayer: Buffer allocation failed (IPLerror).");
        processor.cleanup();
        return;
    }
    if (!sa_out_buffer.data) {
        ResonanceLog::error("ResonanceAmbisonicPlayer: Buffer allocation returned null.");
        processor.cleanup();
        return;
    }

    is_initialized = true;
    ResonanceLog::info("Nexus Resonance: Ambisonic DSP Initialized (Order: " + String::num(ambisonic_order) + ").");
}

void ResonanceAmbisonicInternalPlayback::_process_steam_audio_block() {
    // Crash protection: validate buffers before use
    if (!sa_out_buffer.data || !sa_out_buffer.data[0] || !sa_out_buffer.data[1])
        return;

    int num_channels = (ambisonic_order + 1) * (ambisonic_order + 1);
    size_t block_samples = static_cast<size_t>(frame_size_) * static_cast<size_t>(num_channels);

    // 1. Read interleaved data from ring buffer
    input_ring.read(temp_interleaved_input.data(), block_samples);

    ResonanceServer* srv = ResonanceServer::get_singleton();
    if (srv && srv->is_initialized() && !srv->is_spatial_audio_output_ready()) {
        for (int ch = 0; ch < sa_out_buffer.numChannels && sa_out_buffer.data && sa_out_buffer.data[ch]; ch++)
            memset(sa_out_buffer.data[ch], 0, frame_size_ * sizeof(float));
    } else {
        // 2. Process (Vector-based input)
        processor.process(temp_interleaved_input, sa_out_buffer, params_current.listener_orientation);
    }

    // 3. Write de-interleaved stereo output to rings
    output_ring_l.write(sa_out_buffer.data[0], frame_size_);
    output_ring_r.write(sa_out_buffer.data[1], frame_size_);
}

int32_t ResonanceAmbisonicInternalPlayback::_mix(AudioFrame* buffer, float rate_scale, int32_t frames) {
    if (channel_playbacks.empty())
        return 0;

    _sync_params();

    int num_channels = (ambisonic_order + 1) * (ambisonic_order + 1);
    size_t block_samples = static_cast<size_t>(frame_size_) * static_cast<size_t>(num_channels);

    // 1. Mix first stream to get sample count
    PackedVector2Array buf_0 = channel_playbacks[0]->mix_audio(rate_scale, frames);
    int32_t samples_read = buf_0.size();

    if (samples_read == 0)
        return 0;

    // 2. Collect all channel data (pad missing with 0)
    std::vector<PackedVector2Array> channel_bufs;
    channel_bufs.resize(num_channels);
    for (int c = 0; c < num_channels; c++) {
        if (c < (int)channel_playbacks.size() && channel_playbacks[c].is_valid()) {
            channel_bufs[c] = channel_playbacks[c]->mix_audio(rate_scale, frames);
        }
    }

    // Lazy Init
    if (!is_initialized) {
        _lazy_init_steam_audio();
        if (!is_initialized) {
            for (int i = 0; i < samples_read; i++) {
                float w = (!channel_bufs[0].is_empty() && channel_bufs[0].size() > (unsigned)i)
                              ? channel_bufs[0][i].x
                              : 0.0f;
                buffer[i].left = w;
                buffer[i].right = w;
            }
            return samples_read;
        }
    }

    // 3. Interleave all channels and push to Input Ring (batch write for efficiency)
    size_t interleaved_count = static_cast<size_t>(samples_read) * static_cast<size_t>(num_channels);
    if (temp_interleaved_input.size() < interleaved_count) {
        temp_interleaved_input.resize(interleaved_count);
    }
    for (int i = 0; i < samples_read; i++) {
        for (int c = 0; c < num_channels; c++) {
            float sample = (c < (int)channel_bufs.size() && (int)channel_bufs[c].size() > i)
                               ? channel_bufs[c][i].x
                               : 0.0f;
            temp_interleaved_input[static_cast<size_t>(i) * static_cast<size_t>(num_channels) + static_cast<size_t>(c)] = sample;
        }
    }
    size_t to_write = std::min(interleaved_count, input_ring.get_available_write());
    if (to_write > 0) {
        input_ring.write(temp_interleaved_input.data(), to_write);
    }

    // 4. Process Steam Audio Blocks
    while (input_ring.get_available_read() >= block_samples) {
        if (output_ring_l.get_available_write() >= frame_size_) {
            _process_steam_audio_block();
        } else {
            break;
        }
    }

    // 5. Output (batch read instead of per-sample for efficiency)
    int available = (int)output_ring_l.get_available_read();
    int valid_copy = (samples_read < available) ? samples_read : available;

    if (valid_copy > 0) {
        size_t copy_size = static_cast<size_t>(valid_copy);
        if (temp_output_l.size() < copy_size)
            temp_output_l.resize(copy_size);
        if (temp_output_r.size() < copy_size)
            temp_output_r.resize(copy_size);
        output_ring_l.read(temp_output_l.data(), copy_size);
        output_ring_r.read(temp_output_r.data(), copy_size);
        for (int i = 0; i < valid_copy; i++) {
            buffer[i].left = temp_output_l[i];
            buffer[i].right = temp_output_r[i];
        }
    }

    for (int i = valid_copy; i < samples_read; i++) {
        buffer[i].left = 0.0f;
        buffer[i].right = 0.0f;
    }

    return samples_read;
}

void ResonanceAmbisonicInternalPlayback::_start(double from_pos) {
    for (size_t i = 0; i < channel_playbacks.size(); i++) {
        if (channel_playbacks[i].is_valid())
            channel_playbacks[i]->start(from_pos);
    }
}
void ResonanceAmbisonicInternalPlayback::_stop() {
    for (size_t i = 0; i < channel_playbacks.size(); i++) {
        if (channel_playbacks[i].is_valid())
            channel_playbacks[i]->stop();
    }
}
bool ResonanceAmbisonicInternalPlayback::_is_playing() const {
    return !channel_playbacks.empty() && channel_playbacks[0].is_valid() && channel_playbacks[0]->is_playing();
}
int ResonanceAmbisonicInternalPlayback::_get_loop_count() const {
    return (!channel_playbacks.empty() && channel_playbacks[0].is_valid()) ? channel_playbacks[0]->get_loop_count() : 0;
}
double ResonanceAmbisonicInternalPlayback::_get_playback_position() const {
    return (!channel_playbacks.empty() && channel_playbacks[0].is_valid()) ? channel_playbacks[0]->get_playback_position() : 0.0;
}
void ResonanceAmbisonicInternalPlayback::_seek(double position) {
    for (size_t i = 0; i < channel_playbacks.size(); i++) {
        if (channel_playbacks[i].is_valid())
            channel_playbacks[i]->seek(position);
    }
}

// ============================================================================
// STREAM & PLAYER
// ============================================================================

void ResonanceAmbisonicInternalStream::set_channel_streams(const Array& p_streams) { channel_streams = p_streams; }
Array ResonanceAmbisonicInternalStream::get_channel_streams() const { return channel_streams; }

void ResonanceAmbisonicInternalStream::set_ambisonic_order(int p_order) {
    ambisonic_order = CLAMP(p_order, 1, 3);
}
int ResonanceAmbisonicInternalStream::get_ambisonic_order() const { return ambisonic_order; }

void ResonanceAmbisonicInternalStream::set_stream_w(const Ref<AudioStream>& p_stream) { stream_w = p_stream; }
Ref<AudioStream> ResonanceAmbisonicInternalStream::get_stream_w() const { return stream_w; }

void ResonanceAmbisonicInternalStream::set_stream_y(const Ref<AudioStream>& p_stream) { stream_y = p_stream; }
Ref<AudioStream> ResonanceAmbisonicInternalStream::get_stream_y() const { return stream_y; }

void ResonanceAmbisonicInternalStream::set_stream_z(const Ref<AudioStream>& p_stream) { stream_z = p_stream; }
Ref<AudioStream> ResonanceAmbisonicInternalStream::get_stream_z() const { return stream_z; }

void ResonanceAmbisonicInternalStream::set_stream_x(const Ref<AudioStream>& p_stream) { stream_x = p_stream; }
Ref<AudioStream> ResonanceAmbisonicInternalStream::get_stream_x() const { return stream_x; }

double ResonanceAmbisonicInternalStream::_get_length() const {
    // Length is taken from channel_streams[0] (or stream_w for legacy). All Ambisonic channels
    // are assumed to have identical length; if they differ, channel 0 is the reference.
    // Assumption: all Ambisonic channels have the same length (typical for B-format sources).
    if (!channel_streams.is_empty()) {
        Variant elem = channel_streams[0];
        Object* obj = (elem.get_type() == Variant::OBJECT) ? static_cast<Object*>(elem) : nullptr;
        AudioStream* as = Object::cast_to<AudioStream>(obj);
        if (as)
            return as->get_length();
    }
    return stream_w.is_valid() ? stream_w->get_length() : 0.0;
}

Ref<AudioStreamPlayback> ResonanceAmbisonicInternalStream::_instantiate_playback() const {
    Ref<ResonanceAmbisonicInternalPlayback> playback;
    playback.instantiate();

    Array streams;
    int order = ambisonic_order;

    // Order is derived from channel count when valid (4/9/16). ambisonic_order property may differ.
    int cs_size = channel_streams.size();
    if (resonance::is_valid_ambisonic_channel_count(cs_size)) {
        order = (cs_size == 16) ? 3 : (cs_size == 9 ? 2 : 1);
        for (int i = 0; i < cs_size; i++) {
            Variant elem = channel_streams[i];
            Object* obj = (elem.get_type() == Variant::OBJECT) ? static_cast<Object*>(elem) : nullptr;
            AudioStream* as = Object::cast_to<AudioStream>(obj);
            Ref<AudioStream> s = as ? Ref<AudioStream>(as) : Ref<AudioStream>();
            Ref<AudioStreamPlayback> pb = s.is_valid() ? s->instantiate_playback() : Ref<AudioStreamPlayback>();
            streams.push_back(pb);
        }
    } else if (stream_w.is_valid()) {
        order = 1;
        streams.push_back(stream_w->instantiate_playback());
        streams.push_back(stream_y.is_valid() ? stream_y->instantiate_playback() : Ref<AudioStreamPlayback>());
        streams.push_back(stream_z.is_valid() ? stream_z->instantiate_playback() : Ref<AudioStreamPlayback>());
        streams.push_back(stream_x.is_valid() ? stream_x->instantiate_playback() : Ref<AudioStreamPlayback>());
    } else if (cs_size > 0) {
        // Invalid channel count (use resonance::is_valid_ambisonic_channel_count). Use closest valid order and pad/truncate.
        UtilityFunctions::push_warning("Nexus Resonance: channel_streams size ", cs_size, " is invalid (use 4, 9, or 16). Using order 2 (9 channels).");
        order = 2;
        int target = 9;
        for (int i = 0; i < target; i++) {
            Ref<AudioStreamPlayback> pb;
            if (i < cs_size) {
                Variant elem = channel_streams[i];
                Object* obj = (elem.get_type() == Variant::OBJECT) ? static_cast<Object*>(elem) : nullptr;
                AudioStream* as = Object::cast_to<AudioStream>(obj);
                Ref<AudioStream> s = as ? Ref<AudioStream>(as) : Ref<AudioStream>();
                pb = s.is_valid() ? s->instantiate_playback() : Ref<AudioStreamPlayback>();
            }
            streams.push_back(pb);
        }
    }

    if (streams.is_empty())
        return playback;

    playback->set_channel_playbacks(streams, order);
    return playback;
}

void ResonanceAmbisonicInternalStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_channel_streams", "p_streams"), &ResonanceAmbisonicInternalStream::set_channel_streams);
    ClassDB::bind_method(D_METHOD("get_channel_streams"), &ResonanceAmbisonicInternalStream::get_channel_streams);
    ClassDB::bind_method(D_METHOD("set_ambisonic_order", "p_order"), &ResonanceAmbisonicInternalStream::set_ambisonic_order);
    ClassDB::bind_method(D_METHOD("get_ambisonic_order"), &ResonanceAmbisonicInternalStream::get_ambisonic_order);

    ClassDB::bind_method(D_METHOD("set_stream_w", "p_stream"), &ResonanceAmbisonicInternalStream::set_stream_w);
    ClassDB::bind_method(D_METHOD("get_stream_w"), &ResonanceAmbisonicInternalStream::get_stream_w);
    ClassDB::bind_method(D_METHOD("set_stream_y", "p_stream"), &ResonanceAmbisonicInternalStream::set_stream_y);
    ClassDB::bind_method(D_METHOD("get_stream_y"), &ResonanceAmbisonicInternalStream::get_stream_y);
    ClassDB::bind_method(D_METHOD("set_stream_z", "p_stream"), &ResonanceAmbisonicInternalStream::set_stream_z);
    ClassDB::bind_method(D_METHOD("get_stream_z"), &ResonanceAmbisonicInternalStream::get_stream_z);
    ClassDB::bind_method(D_METHOD("set_stream_x", "p_stream"), &ResonanceAmbisonicInternalStream::set_stream_x);
    ClassDB::bind_method(D_METHOD("get_stream_x"), &ResonanceAmbisonicInternalStream::get_stream_x);

    ADD_GROUP("Ambisonics", "");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "channel_streams", PROPERTY_HINT_ARRAY_TYPE, "AudioStream"), "set_channel_streams", "get_channel_streams");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ambisonic_order", PROPERTY_HINT_RANGE, "1,3,1"), "set_ambisonic_order", "get_ambisonic_order");

    ADD_GROUP("Legacy (1st Order)", "");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stream_w", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_stream_w", "get_stream_w");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stream_y", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_stream_y", "get_stream_y");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stream_z", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_stream_z", "get_stream_z");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stream_x", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_stream_x", "get_stream_x");
}

void ResonanceAmbisonicPlayer::_ready() {
    // If stream is set in editor, we just play.
    // The stream must be a ResonanceAmbisonicInternalStream resource.
    if (is_autoplay_enabled())
        play();
}

void ResonanceAmbisonicPlayer::_process(double delta) {
    if (!is_playing())
        return;

    IPLCoordinateSpace3 listener_orient{};

    Viewport* vp = get_viewport();
    if (vp && vp->get_camera_3d()) {
        Camera3D* cam = vp->get_camera_3d();
        Vector3 forward = -cam->get_global_transform().basis.get_column(2);
        Vector3 up = cam->get_global_transform().basis.get_column(1);
        Vector3 right = cam->get_global_transform().basis.get_column(0);

        listener_orient.origin = {0, 0, 0};
        listener_orient.ahead = {forward.x, forward.y, forward.z};
        listener_orient.up = {up.x, up.y, up.z};
        listener_orient.right = {right.x, right.y, right.z};
    } else {
        // Fallback: use identity orientation when no viewport/camera (avoids degenerate coord system)
        listener_orient.origin = {0, 0, 0};
        listener_orient.ahead = {0, 0, -1};
        listener_orient.up = {0, 1, 0};
        listener_orient.right = {1, 0, 0};
    }

    AmbisonicPlaybackParameters params;
    params.listener_orientation = listener_orient;
    params.rotation_enabled = rotation_enabled;

    Ref<AudioStreamPlayback> pb = get_stream_playback();
    if (pb.is_valid()) {
        ResonanceAmbisonicInternalPlayback* res_pb = Object::cast_to<ResonanceAmbisonicInternalPlayback>(pb.ptr());
        if (res_pb)
            res_pb->update_parameters(params);
    }
}

void ResonanceAmbisonicPlayer::set_rotation_enabled(bool p_enabled) {
    rotation_enabled = p_enabled;
}

void ResonanceAmbisonicPlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_rotation_enabled", "enabled"), &ResonanceAmbisonicPlayer::set_rotation_enabled);
    ClassDB::bind_method(D_METHOD("is_rotation_enabled"), &ResonanceAmbisonicPlayer::is_rotation_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rotation_enabled"), "set_rotation_enabled", "is_rotation_enabled");
}
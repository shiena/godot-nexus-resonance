#ifndef RESONANCE_AMBISONIC_PLAYER_H
#define RESONANCE_AMBISONIC_PLAYER_H

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/array.hpp>
#include <phonon.h>
#include <vector>
#include <atomic>

#include "resonance_processor_ambisonic.h"
#include "resonance_ring_buffer.h"

namespace godot {

    struct AmbisonicPlaybackParameters {
        IPLCoordinateSpace3 listener_orientation{};
        bool rotation_enabled = true;
    };

    class ResonanceAmbisonicInternalPlayback : public AudioStreamPlayback {
        GDCLASS(ResonanceAmbisonicInternalPlayback, AudioStreamPlayback)

    private:
        static const int kMaxAmbisonicChannels = 16; // 3rd Order max
        int frame_size_ = 512;  // Steam Audio block size from ResonanceServer (256/512/1024)

        // Playbacks for each Ambisonic channel (4 for 1st, 9 for 2nd, 16 for 3rd order)
        std::vector<Ref<AudioStreamPlayback>> channel_playbacks;
        int ambisonic_order = 1;

        std::atomic<bool> params_dirty = false;
        AmbisonicPlaybackParameters params_next;
        AmbisonicPlaybackParameters params_current;

        bool is_initialized = false;
        int current_sample_rate = 48000;
        IPLContext context = nullptr;

        ResonanceAmbisonicProcessor processor;

        IPLAudioBuffer sa_out_buffer{};

        // RingBuffers
        // Input: Interleaved N channels (N = (order+1)^2)
        RingBuffer<float> input_ring;
        // Output: Stereo (Left/Right)
        RingBuffer<float> output_ring_l;
        RingBuffer<float> output_ring_r;

        // Temp buffer for processing loop
        std::vector<float> temp_interleaved_input;

        void _lazy_init_steam_audio(int sampling_rate);
        void _cleanup_steam_audio();
        void _process_steam_audio_block();
        void _sync_params();

    public:
        ResonanceAmbisonicInternalPlayback();
        ~ResonanceAmbisonicInternalPlayback();

        void set_channel_playbacks(const Array& playbacks, int p_order);

        void update_parameters(const AmbisonicPlaybackParameters& p_params);

        virtual int32_t _mix(AudioFrame* buffer, double rate_scale, int32_t frames);
        virtual void _start(double from_pos) override;
        virtual void _stop() override;
        virtual bool _is_playing() const override;
        virtual int _get_loop_count() const override;
        virtual void _seek(double position) override;

    protected:
        static void _bind_methods() {}
    };

    class ResonanceAmbisonicInternalStream : public AudioStream {
        GDCLASS(ResonanceAmbisonicInternalStream, AudioStream)
    private:
        Array channel_streams;
        int ambisonic_order = 1;

        // Backward compatibility: 1st order can use these 4 streams
        Ref<AudioStream> stream_w;
        Ref<AudioStream> stream_y;
        Ref<AudioStream> stream_z;
        Ref<AudioStream> stream_x;
    public:
        void set_channel_streams(const Array& p_streams);
        Array get_channel_streams() const;

        void set_ambisonic_order(int p_order);
        int get_ambisonic_order() const;

        // Deprecated: use channel_streams for 1st order. Kept for backward compatibility.
        void set_stream_w(const Ref<AudioStream>& p_stream);
        Ref<AudioStream> get_stream_w() const;

        void set_stream_y(const Ref<AudioStream>& p_stream);
        Ref<AudioStream> get_stream_y() const;

        void set_stream_z(const Ref<AudioStream>& p_stream);
        Ref<AudioStream> get_stream_z() const;

        void set_stream_x(const Ref<AudioStream>& p_stream);
        Ref<AudioStream> get_stream_x() const;

        virtual Ref<AudioStreamPlayback> _instantiate_playback() const override;
        virtual String _get_stream_name() const override { return "ResonanceAmbisonicInternal"; }
        virtual double _get_length() const override;
        virtual bool _is_monophonic() const override { return false; }

    protected:
        static void _bind_methods();
    };

    class ResonanceAmbisonicPlayer : public AudioStreamPlayer {
        GDCLASS(ResonanceAmbisonicPlayer, AudioStreamPlayer)
    private:
        bool rotation_enabled = true;  ///< Apply Ambisonics rotation (listener orientation). Disable for fixed/world-space decode.
    protected:
        static void _bind_methods();

    public:
        ResonanceAmbisonicPlayer();
        ~ResonanceAmbisonicPlayer();

        void _ready() override;
        void _process(double delta) override;

        void set_rotation_enabled(bool p_enabled);
        bool is_rotation_enabled() const { return rotation_enabled; }

        // This player uses the standard "stream" property of AudioStreamPlayer.
        // The user must assign a ResonanceAmbisonicInternalStream to it.
        // We provide a helper to play easily.
    };

} // namespace godot

#endif
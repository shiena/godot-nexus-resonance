#ifndef RESONANCE_PROCESSOR_AMBISONIC_H
#define RESONANCE_PROCESSOR_AMBISONIC_H

#include <phonon.h>
#include <vector>

namespace godot {

    class ResonanceAmbisonicProcessor {
    private:
        IPLContext context = nullptr;
        IPLAmbisonicsRotationEffect rotation_effect = nullptr;
        IPLAmbisonicsDecodeEffect ambisonics_dec_effect = nullptr;

        // Buffers
        IPLAudioBuffer sa_in_buffer{};   // 4 Channels (Ambisonic B-Format)
        IPLAudioBuffer sa_rotated_buffer{};  // Rotated Ambisonics (listener-space)
        IPLAudioBuffer sa_out_buffer{};  // 2 Channels (Stereo/Binaural)

        bool is_initialized = false;
        int frame_size = 1024;
        int sample_rate = 48000;
        int ambisonic_order = 1;
        bool rotation_enabled = true;

    public:
        ResonanceAmbisonicProcessor();
        ~ResonanceAmbisonicProcessor();

        void initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_order, bool p_rotation_enabled = true);
        void cleanup();

        // Process 4-channel input -> 2-channel output based on orientation
        void process(const std::vector<float>& input_data, IPLAudioBuffer& out_buffer,
            const IPLCoordinateSpace3& listener_orient);
    };

} // namespace godot

#endif
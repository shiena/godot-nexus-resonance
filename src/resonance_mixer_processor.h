#ifndef RESONANCE_MIXER_PROCESSOR_H
#define RESONANCE_MIXER_PROCESSOR_H

#include <phonon.h>
#include <godot_cpp/classes/audio_frame.hpp>
#include "resonance_utils.h"

namespace godot {

    class ResonanceMixerProcessor {
    private:
        IPLContext context = nullptr;
        IPLAmbisonicsDecodeEffect decode_effect = nullptr;
        // Virtual Surround path: decode to 7.1 -> iplVirtualSurroundEffect -> stereo
        IPLAmbisonicsDecodeEffect decode_effect_7_1 = nullptr;
        IPLVirtualSurroundEffect virtual_surround_effect = nullptr;

        // Buffers
        IPLAudioBuffer sa_ambisonic_buffer{};   // Mixer -> Decode
        IPLAudioBuffer sa_stereo_buffer{};     // Decode -> Godot (or VirtualSurround -> Godot)
        IPLAudioBuffer sa_7_1_buffer{};        // Decode 7.1 intermediate when use_virtual_surround

        void _write_stereo_to_audio_frames(float* left, float* right, AudioFrame* out_frames, int frame_count);
        void _decode_ambisonic_to_stereo_buffer(IPLAudioBuffer* ambi_in, const IPLCoordinateSpace3& listener_coords);

        bool is_initialized = false;
        int frame_size = 1024;
        int ambisonic_order = 1;

    public:
        ResonanceMixerProcessor();
        ~ResonanceMixerProcessor();

        void initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order);
        void cleanup();

        // Pulls audio from the provided mixer handle, decodes it using listener orientation, and writes to output.
        bool process_mixer_return(IPLReflectionMixer mixer_handle, const IPLCoordinateSpace3& listener_coords, AudioFrame* out_frames, int frame_count);

        // Decode external ambisonic buffer to stereo (bypasses mixer - for direct convolution output)
        bool decode_ambisonic_to_stereo(IPLAudioBuffer* ambi_buf, const IPLCoordinateSpace3& listener_coords, AudioFrame* out_frames, int frame_count);
    };

} // namespace godot

#endif
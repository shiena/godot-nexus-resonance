#ifndef RESONANCE_PROCESSOR_PATH_H
#define RESONANCE_PROCESSOR_PATH_H

#include <phonon.h>

namespace godot {

    class ResonancePathProcessor {
    private:
        IPLContext context = nullptr;
        IPLPathEffect path_effect = nullptr;
        IPLAudioBuffer internal_mono_buffer{};
        IPLAudioBuffer internal_stereo_buffer{};

        bool is_initialized = false;
        int frame_size = 1024;
        int ambisonic_order = 1;

    public:
        ResonancePathProcessor();
        ~ResonancePathProcessor();

        void initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order);
        void cleanup();

        void process(const IPLAudioBuffer& in_buffer, const IPLPathEffectParams& params, IPLAudioBuffer& out_buffer);
    };

} // namespace godot

#endif

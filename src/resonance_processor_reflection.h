#ifndef RESONANCE_PROCESSOR_REFLECTION_H
#define RESONANCE_PROCESSOR_REFLECTION_H

#include <phonon.h>

namespace godot {

    class ResonanceReflectionProcessor {
    private:
        IPLContext context = nullptr;
        IPLReflectionEffect reflection_effect = nullptr;

        // Intermediate Buffer (Mono Input for Convolution)
        IPLAudioBuffer sa_mono_buffer{};
        IPLAudioBuffer sa_temp_out_buffer{}; // Required by API even if mixing

        bool is_initialized = false;
        int frame_size = 1024;
        int sample_rate = 48000;
        int num_channels = 4; // Ambisonic channels for convolution, 1 for parametric
        int reflection_type = 0; // 0 = Convolution, 1 = Parametric

    public:
        ResonanceReflectionProcessor();
        ~ResonanceReflectionProcessor();

        void initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order, int p_reflection_type = 0);
        void cleanup();

        // Mixes into the provided Mixer handle (unused if using direct path).
        // reverb_gain scales the input before convolution (for distance/occlusion/air absorption).
        void process_mix(const IPLAudioBuffer& in_buffer,
            const IPLReflectionEffectParams& reverb_params,
            IPLReflectionMixer mixer_handle,
            float reverb_gain = 1.0f);

        /// Bypass mixer: apply convolution with mixer=null, output in internal buffer.
        /// Returns pointer to sa_temp_out_buffer (ambisonic) for external decode. Valid until next process call.
        /// Note: reverb_gain is NOT applied here (caller applies attenuation/transmission/air to reverb mix level separately).
        void process_mix_direct(const IPLAudioBuffer& in_buffer, const IPLReflectionEffectParams& reverb_params);
        IPLAudioBuffer* get_direct_output_buffer() { return &sa_temp_out_buffer; }
        bool is_parametric() const { return reflection_type == 1; }
    };

} // namespace godot

#endif
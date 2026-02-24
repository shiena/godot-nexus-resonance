#ifndef RESONANCE_PROCESSOR_DIRECT_H
#define RESONANCE_PROCESSOR_DIRECT_H

#include <phonon.h>
#include <vector>

namespace godot {

    class ResonanceDirectProcessor {
    private:
        IPLContext context = nullptr;
        IPLHRTF hrtf_handle = nullptr; // <--- NEW: Store reference to global HRTF

        // Effects chain
        IPLDirectEffect direct_effect = nullptr;
        IPLBinauralEffect binaural_effect = nullptr;
        IPLPanningEffect panning_effect = nullptr;
        IPLAmbisonicsEncodeEffect ambisonics_encode_effect = nullptr;
        IPLAmbisonicsBinauralEffect ambisonics_binaural_effect = nullptr;

        // Intermediate buffers
        IPLAudioBuffer internal_mono_buffer{};
        IPLAudioBuffer internal_ambi_buffer{};

        // Stored for tail processing when source stops
        IPLVector3 last_direction = { 0.0f, 0.0f, -1.0f };
        bool last_hrtf_bilinear = false;
        float last_spatial_blend = 1.0f;
        bool last_use_ambisonics_encode_path = false;

        bool is_initialized = false;
        int frame_size = 1024;
        int ambisonic_order = 1;
        bool use_ambisonics_encode = false;

    public:
        ResonanceDirectProcessor();
        ~ResonanceDirectProcessor();

        void initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order = 1, bool p_use_ambisonics_encode = false);
        void cleanup();

        void process(
            bool use_ambisonics_encode_path,
            const IPLAudioBuffer& in_buffer,
            IPLAudioBuffer& out_buffer,
            float attenuation,
            float occlusion,
            const float* transmission,
            const float* air_absorption,
            bool apply_air_absorption,
            float directivity_value,
            bool apply_directivity,
            bool apply_effect,
            bool use_binaural,
            int transmission_type,
            bool hrtf_interpolation_bilinear,
            float spatial_blend,
            const IPLCoordinateSpace3& listener_coords,
            const IPLVector3& source_pos
        );

        /// Get tail samples when input has stopped. Returns true if tail was output, false if no tail remaining.
        /// Output is stereo (spatialized with last direction). Call repeatedly until returns false.
        bool process_tail(IPLAudioBuffer& out_buffer);

        /// Reset internal state (e.g. when starting new playback). Clears any pending tail.
        void reset_for_new_playback();

    private:
        void apply_spatialization(const IPLVector3& dir, IPLAudioBuffer& out,
            bool use_ambi_path, bool use_binaural, bool hrtf_bilinear, float spatial_blend);
    };
} // namespace godot

#endif
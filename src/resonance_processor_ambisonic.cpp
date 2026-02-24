#include "resonance_processor_ambisonic.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>
#include <vector>

namespace godot {

    ResonanceAmbisonicProcessor::ResonanceAmbisonicProcessor() {}

    ResonanceAmbisonicProcessor::~ResonanceAmbisonicProcessor() {
        cleanup();
    }

    void ResonanceAmbisonicProcessor::initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_order, bool p_rotation_enabled) {
        if (is_initialized) return;

        context = p_context;
        frame_size = p_frame_size;
        sample_rate = p_sample_rate;
        ambisonic_order = p_order;
        rotation_enabled = p_rotation_enabled;

        IPLAudioSettings audioSettings{};
        audioSettings.samplingRate = sample_rate;
        audioSettings.frameSize = frame_size;

        int num_channels = (ambisonic_order + 1) * (ambisonic_order + 1);

        // 1. Rotation effect: rotates world-space Ambisonics to listener-space (optional, for head-tracking)
        if (rotation_enabled) {
            IPLAmbisonicsRotationEffectSettings rotSettings{};
            rotSettings.maxOrder = ambisonic_order;
            IPLerror rot_status = iplAmbisonicsRotationEffectCreate(context, &audioSettings, &rotSettings, &rotation_effect);
            if (rot_status != IPL_STATUS_SUCCESS) {
                UtilityFunctions::push_error("Nexus Resonance: iplAmbisonicsRotationEffectCreate failed.");
                return;
            }
        }

        // 2. Decode effect: listener-space Ambisonics -> stereo
        IPLAmbisonicsDecodeEffectSettings decSettings{};
        decSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
        decSettings.speakerLayout.numSpeakers = 2;
        decSettings.maxOrder = ambisonic_order;

        IPLerror dec_status = iplAmbisonicsDecodeEffectCreate(context, &audioSettings, &decSettings, &ambisonics_dec_effect);
        if (dec_status != IPL_STATUS_SUCCESS) {
            UtilityFunctions::push_error("Nexus Resonance: iplAmbisonicsDecodeEffectCreate failed.");
            if (rotation_effect) {
                iplAmbisonicsRotationEffectRelease(&rotation_effect);
                rotation_effect = nullptr;
            }
            return;
        }

        IPLerror buf_status = iplAudioBufferAllocate(context, num_channels, frame_size, &sa_in_buffer);
        if (buf_status != IPL_STATUS_SUCCESS) {
            UtilityFunctions::push_error("Nexus Resonance: Ambisonic buffer allocation failed.");
            cleanup();
            return;
        }
        buf_status = iplAudioBufferAllocate(context, num_channels, frame_size, &sa_rotated_buffer);
        if (buf_status != IPL_STATUS_SUCCESS) {
            UtilityFunctions::push_error("Nexus Resonance: Ambisonic rotated buffer allocation failed.");
            cleanup();
            return;
        }

        is_initialized = true;
    }

    void ResonanceAmbisonicProcessor::cleanup() {
        if (rotation_effect) iplAmbisonicsRotationEffectRelease(&rotation_effect);
        if (ambisonics_dec_effect) iplAmbisonicsDecodeEffectRelease(&ambisonics_dec_effect);
        if (context) {
            iplAudioBufferFree(context, &sa_in_buffer);
            iplAudioBufferFree(context, &sa_rotated_buffer);
        }
        memset(&sa_in_buffer, 0, sizeof(sa_in_buffer));
        memset(&sa_rotated_buffer, 0, sizeof(sa_rotated_buffer));

        rotation_effect = nullptr;
        ambisonics_dec_effect = nullptr;
        context = nullptr;
        is_initialized = false;
    }

    void ResonanceAmbisonicProcessor::process(const std::vector<float>& input_data, IPLAudioBuffer& out_buffer,
        const IPLCoordinateSpace3& listener_orient) {

        if (!is_initialized || !ambisonics_dec_effect || !out_buffer.data) {
            // Silence (with null checks to avoid crash on unallocated buffer)
            for (int i = 0; i < out_buffer.numChannels && out_buffer.data && out_buffer.data[i]; i++) {
                memset(out_buffer.data[i], 0, frame_size * sizeof(float));
            }
            return;
        }

        // 1. Deinterleave Input (std::vector flat -> IPLAudioBuffer 4ch)
        // Input data is expected to be 4 channels interleaved: [W, Y, Z, X] (Ambisonics B-Format)
        // IPL API has non-const param; input is read-only
        iplAudioBufferDeinterleave(context, const_cast<float*>(input_data.data()), &sa_in_buffer);

        // 2. Rotation: world-space Ambisonics -> listener-space (optional; when disabled, pass through)
        IPLAudioBuffer* decode_input = &sa_in_buffer;
        if (rotation_effect) {
            IPLAmbisonicsRotationEffectParams rotParams{};
            rotParams.orientation = listener_orient;
            rotParams.order = ambisonic_order;
            iplAmbisonicsRotationEffectApply(rotation_effect, &rotParams, &sa_in_buffer, &sa_rotated_buffer);
            decode_input = &sa_rotated_buffer;
        }

        // 3. Decode: Ambisonics -> stereo
        IPLAmbisonicsDecodeEffectParams decParams{};
        decParams.order = ambisonic_order;
        decParams.hrtf = nullptr;
        decParams.orientation.ahead = { 0, 0, -1 };
        decParams.orientation.up = { 0, 1, 0 };
        decParams.orientation.right = { 1, 0, 0 };
        decParams.orientation.origin = { 0, 0, 0 };
        decParams.binaural = IPL_FALSE;

        iplAmbisonicsDecodeEffectApply(ambisonics_dec_effect, &decParams, decode_input, &out_buffer);
    }

} // namespace godot
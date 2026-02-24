#include "resonance_mixer_processor.h"
#include "resonance_server.h"
#include "resonance_math.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstdio>
#include <cmath>

namespace godot {

    ResonanceMixerProcessor::ResonanceMixerProcessor() {}
    ResonanceMixerProcessor::~ResonanceMixerProcessor() { cleanup(); }

    void ResonanceMixerProcessor::initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order) {
        if (is_initialized) return;

        context = p_context;
        frame_size = p_frame_size;
        ambisonic_order = p_ambisonic_order;

        IPLAudioSettings audioSettings{ p_sample_rate, p_frame_size };
        int num_channels = (ambisonic_order + 1) * (ambisonic_order + 1);
        iplAudioBufferAllocate(context, num_channels, frame_size, &sa_ambisonic_buffer);
        iplAudioBufferAllocate(context, 2, frame_size, &sa_stereo_buffer);

        ResonanceServer* srv = ResonanceServer::get_singleton();
        bool use_vs = srv && srv->use_virtual_surround_output();
        IPLHRTF hrtf_handle = srv ? srv->get_hrtf_handle() : nullptr;

        // 1. Create Decoder Effect (stereo for standard path)
        IPLAmbisonicsDecodeEffectSettings decSettings{};
        decSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
        decSettings.speakerLayout.numSpeakers = 2;
        decSettings.maxOrder = ambisonic_order;
        decSettings.hrtf = use_vs ? nullptr : (srv && srv->use_reverb_binaural() ? hrtf_handle : nullptr);

        iplAmbisonicsDecodeEffectCreate(context, &audioSettings, &decSettings, &decode_effect);

        // 2. Virtual Surround path: decode to 7.1, then VirtualSurround -> stereo
        if (use_vs && hrtf_handle) {
            IPLAmbisonicsDecodeEffectSettings dec7Settings{};
            dec7Settings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_SURROUND_7_1;
            dec7Settings.speakerLayout.numSpeakers = 8;
            dec7Settings.maxOrder = ambisonic_order;
            dec7Settings.hrtf = nullptr;

            iplAmbisonicsDecodeEffectCreate(context, &audioSettings, &dec7Settings, &decode_effect_7_1);
            iplAudioBufferAllocate(context, 8, frame_size, &sa_7_1_buffer);

            IPLVirtualSurroundEffectSettings vsSettings{};
            vsSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_SURROUND_7_1;
            vsSettings.speakerLayout.numSpeakers = 8;
            vsSettings.hrtf = hrtf_handle;

            if (iplVirtualSurroundEffectCreate(context, &audioSettings, &vsSettings, &virtual_surround_effect) != IPL_STATUS_SUCCESS) {
                if (decode_effect_7_1) { iplAmbisonicsDecodeEffectRelease(&decode_effect_7_1); decode_effect_7_1 = nullptr; }
                iplAudioBufferFree(context, &sa_7_1_buffer);
            }
        }

        is_initialized = true;
    }

    void ResonanceMixerProcessor::cleanup() {
        if (decode_effect) iplAmbisonicsDecodeEffectRelease(&decode_effect);
        if (decode_effect_7_1) iplAmbisonicsDecodeEffectRelease(&decode_effect_7_1);
        if (virtual_surround_effect) iplVirtualSurroundEffectRelease(&virtual_surround_effect);
        decode_effect = nullptr;
        decode_effect_7_1 = nullptr;
        virtual_surround_effect = nullptr;
        if (context) {
            iplAudioBufferFree(context, &sa_ambisonic_buffer);
            iplAudioBufferFree(context, &sa_stereo_buffer);
            if (sa_7_1_buffer.data) iplAudioBufferFree(context, &sa_7_1_buffer);
        }
        sa_7_1_buffer.data = nullptr;
        is_initialized = false;
        context = nullptr;
    }

    void ResonanceMixerProcessor::_write_stereo_to_audio_frames(float* left, float* right, AudioFrame* out_frames, int frame_count) {
        if (!left || !right || !out_frames || frame_count <= 0) return;
        for (int i = 0; i < frame_count; i++) {
            out_frames[i].left += resonance::sanitize_audio_float(left[i]);
            out_frames[i].right += resonance::sanitize_audio_float(right[i]);
        }
    }

    void ResonanceMixerProcessor::_decode_ambisonic_to_stereo_buffer(IPLAudioBuffer* ambi_in, const IPLCoordinateSpace3& listener_coords) {
        if (!ambi_in || !ambi_in->data) return;
        IPLAmbisonicsDecodeEffectParams decParams{};
        decParams.order = ambisonic_order;
        decParams.orientation = listener_coords;
        ResonanceServer* srv = ResonanceServer::get_singleton();
        bool use_vs = srv && srv->use_virtual_surround_output() && decode_effect_7_1 && virtual_surround_effect;

        if (use_vs) {
            decParams.hrtf = nullptr;
            decParams.binaural = IPL_FALSE;
            iplAmbisonicsDecodeEffectApply(decode_effect_7_1, &decParams, ambi_in, &sa_7_1_buffer);
            IPLVirtualSurroundEffectParams vsParams{};
            vsParams.hrtf = srv->get_hrtf_handle();
            iplVirtualSurroundEffectApply(virtual_surround_effect, &vsParams, &sa_7_1_buffer, &sa_stereo_buffer);
        } else {
            decParams.hrtf = (srv && srv->use_reverb_binaural()) ? srv->get_hrtf_handle() : nullptr;
            decParams.binaural = (srv && srv->use_reverb_binaural()) ? IPL_TRUE : IPL_FALSE;
            iplAmbisonicsDecodeEffectApply(decode_effect, &decParams, ambi_in, &sa_stereo_buffer);
        }
    }

    bool ResonanceMixerProcessor::process_mixer_return(IPLReflectionMixer mixer_handle, const IPLCoordinateSpace3& listener_coords, AudioFrame* out_frames, int frame_count) {
        if (!is_initialized || !mixer_handle || !decode_effect) return false;

        // 1. Apply Mixer: Extracts the accumulated audio from the mixer into our local Ambisonic buffer.
        // The mixer apply step consumes accumulated audio; params are required by API but not used for convolution.
        // Use PARAMETRIC type so validation does not require ir/irSize (avoids Steam Audio validation warnings).
        IPLReflectionEffectParams params{};
        params.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
        params.numChannels = sa_ambisonic_buffer.numChannels;
        params.reverbTimes[0] = params.reverbTimes[1] = params.reverbTimes[2] = 0.5f;  // Dummy; not used for mixer consumption

        iplReflectionMixerApply(mixer_handle, &params, &sa_ambisonic_buffer);

        _decode_ambisonic_to_stereo_buffer(&sa_ambisonic_buffer, listener_coords);

        int safe_frames = (frame_count < frame_size) ? frame_count : frame_size;
        _write_stereo_to_audio_frames(sa_stereo_buffer.data[0], sa_stereo_buffer.data[1], out_frames, safe_frames);
        return true;
    }

    bool ResonanceMixerProcessor::decode_ambisonic_to_stereo(IPLAudioBuffer* ambi_buf,
        const IPLCoordinateSpace3& listener_coords, AudioFrame* out_frames, int frame_count) {
        if (!is_initialized || !decode_effect || !ambi_buf || !ambi_buf->data) return false;

        _decode_ambisonic_to_stereo_buffer(ambi_buf, listener_coords);

        int safe_frames = (frame_count < frame_size) ? frame_count : frame_size;
        _write_stereo_to_audio_frames(sa_stereo_buffer.data[0], sa_stereo_buffer.data[1], out_frames, safe_frames);
        return true;
    }
}
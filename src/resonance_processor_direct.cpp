#include "resonance_processor_direct.h"
#include "resonance_utils.h"
#include "resonance_server.h"
#include "resonance_log.h" 
#include <cstring>
#include <algorithm> 

namespace godot {

    ResonanceDirectProcessor::ResonanceDirectProcessor() {}

    ResonanceDirectProcessor::~ResonanceDirectProcessor() { cleanup(); }

    void ResonanceDirectProcessor::initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order, bool p_use_ambisonics_encode) {
        if (is_initialized) return;

        if (!p_context) {
            ResonanceLog::error("DirectProcessor: Context is null!");
            return;
        }

        context = p_context;
        frame_size = p_frame_size;
        ambisonic_order = p_ambisonic_order;
        use_ambisonics_encode = p_use_ambisonics_encode;

        IPLAudioSettings audioSettings{};
        audioSettings.samplingRate = p_sample_rate;
        audioSettings.frameSize = frame_size;

        // 1. Direct Effect (Mono physics)
        IPLDirectEffectSettings directSettings{};
        directSettings.numChannels = 1;

        if (iplDirectEffectCreate(context, &audioSettings, &directSettings, &direct_effect) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("Failed to create IPLDirectEffect");
            return;
        }

        // 2. Binaural Effect
        ResonanceServer* srv = ResonanceServer::get_singleton();
        hrtf_handle = (srv) ? srv->get_hrtf_handle() : nullptr;

        if (hrtf_handle) {
            IPLBinauralEffectSettings binSettings{};
            binSettings.hrtf = hrtf_handle;
            if (iplBinauralEffectCreate(context, &audioSettings, &binSettings, &binaural_effect) != IPL_STATUS_SUCCESS) {
                ResonanceLog::error("Failed to create IPLBinauralEffect");
            }
        }
        else {
            ResonanceLog::warn("DirectProcessor: No HRTF found. Binaural disabled.");
        }

        // 3. Panning Effect
        IPLPanningEffectSettings panSettings{};
        panSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
        panSettings.speakerLayout.numSpeakers = 2;
        if (iplPanningEffectCreate(context, &audioSettings, &panSettings, &panning_effect) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("Failed to create IPLPanningEffect");
        }

        // 4. Optional: Ambisonics Encode + Binaural path (for mixing scenarios)
        if (use_ambisonics_encode && hrtf_handle) {
            IPLAmbisonicsEncodeEffectSettings encSettings{};
            encSettings.maxOrder = ambisonic_order;
            if (iplAmbisonicsEncodeEffectCreate(context, &audioSettings, &encSettings, &ambisonics_encode_effect) != IPL_STATUS_SUCCESS) {
                ResonanceLog::warn("DirectProcessor: Ambisonics Encode effect creation failed.");
            }
            IPLAmbisonicsBinauralEffectSettings ambBinSettings{};
            ambBinSettings.hrtf = hrtf_handle;
            ambBinSettings.maxOrder = ambisonic_order;
            if (iplAmbisonicsBinauralEffectCreate(context, &audioSettings, &ambBinSettings, &ambisonics_binaural_effect) != IPL_STATUS_SUCCESS) {
                ResonanceLog::warn("DirectProcessor: Ambisonics Binaural effect creation failed.");
                if (ambisonics_encode_effect) {
                    iplAmbisonicsEncodeEffectRelease(&ambisonics_encode_effect);
                    ambisonics_encode_effect = nullptr;
                }
            }
            if (ambisonics_encode_effect && ambisonics_binaural_effect) {
                int num_ambi_channels = (ambisonic_order + 1) * (ambisonic_order + 1);
                if (iplAudioBufferAllocate(context, num_ambi_channels, frame_size, &internal_ambi_buffer) != IPL_STATUS_SUCCESS ||
                    !internal_ambi_buffer.data) {
                    iplAmbisonicsEncodeEffectRelease(&ambisonics_encode_effect);
                    iplAmbisonicsBinauralEffectRelease(&ambisonics_binaural_effect);
                    ambisonics_encode_effect = nullptr;
                    ambisonics_binaural_effect = nullptr;
                }
            }
        }

        // 5. Buffer
        if (iplAudioBufferAllocate(context, 1, frame_size, &internal_mono_buffer) != IPL_STATUS_SUCCESS ||
            !internal_mono_buffer.data) {
            ResonanceLog::error("DirectProcessor: Internal buffer allocation failed.");
            cleanup();
            return;
        }

        is_initialized = true;
        ResonanceLog::info("DirectProcessor initialized successfully.");
    }

    void ResonanceDirectProcessor::cleanup() {
        if (direct_effect) { iplDirectEffectRelease(&direct_effect); direct_effect = nullptr; }
        if (binaural_effect) { iplBinauralEffectRelease(&binaural_effect); binaural_effect = nullptr; }
        if (panning_effect) { iplPanningEffectRelease(&panning_effect); panning_effect = nullptr; }
        if (ambisonics_encode_effect) { iplAmbisonicsEncodeEffectRelease(&ambisonics_encode_effect); ambisonics_encode_effect = nullptr; }
        if (ambisonics_binaural_effect) { iplAmbisonicsBinauralEffectRelease(&ambisonics_binaural_effect); ambisonics_binaural_effect = nullptr; }

        if (context && internal_mono_buffer.data != nullptr) {
            iplAudioBufferFree(context, &internal_mono_buffer);
        }
        if (context && internal_ambi_buffer.data != nullptr) {
            iplAudioBufferFree(context, &internal_ambi_buffer);
        }
        internal_mono_buffer.data = nullptr;
        internal_ambi_buffer.data = nullptr;
        hrtf_handle = nullptr;
        context = nullptr;
        is_initialized = false;
    }

    void ResonanceDirectProcessor::process(
        bool use_ambisonics_encode_path,
        const IPLAudioBuffer& in_buffer,
        IPLAudioBuffer& out_buffer,
        float attenuation, float occlusion, const float* transmission, const float* air_absorption, bool apply_air_absorption,
        float directivity_value, bool apply_directivity, bool apply_effect,
        bool use_binaural,
        int transmission_type,
        bool hrtf_interpolation_bilinear,
        float spatial_blend,
        const IPLCoordinateSpace3& listener_coords,
        const IPLVector3& source_pos) {

        // --- PERFORMANCE CRITICAL SECTION ---

        // 1. Validation (Keep these checks, they are cheap and prevent crashes)
        if (!is_initialized || !context) return;
        if (!in_buffer.data || !out_buffer.data || !internal_mono_buffer.data) return;

        // Reset output
        for (int i = 0; i < out_buffer.numChannels; i++) {
            if (out_buffer.data[i]) memset(out_buffer.data[i], 0, frame_size * sizeof(float));
        }

        if (!direct_effect || !apply_effect) return;

        // 2. Downmix Input to Mono (IPL API has non-const param; input is read-only)
        iplAudioBufferDownmix(context, const_cast<IPLAudioBuffer*>(&in_buffer), &internal_mono_buffer);

        // 3. Apply Physics (Occlusion, Transmission, Attenuation)
        IPLDirectEffectParams params{};
        params.flags = static_cast<IPLDirectEffectFlags>(
            IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION |
            IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION |
            IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION);

        params.occlusion = occlusion;
        params.transmissionType = (transmission_type == 1) ? IPL_TRANSMISSIONTYPE_FREQDEPENDENT : IPL_TRANSMISSIONTYPE_FREQINDEPENDENT;

        if (transmission) {
            params.transmission[0] = transmission[0]; params.transmission[1] = transmission[1]; params.transmission[2] = transmission[2];
        }
        else {
            params.transmission[0] = 1.0f; params.transmission[1] = 1.0f; params.transmission[2] = 1.0f;
        }

        if (apply_air_absorption) {
            params.flags = static_cast<IPLDirectEffectFlags>(params.flags | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION);
            params.airAbsorption[0] = air_absorption[0]; params.airAbsorption[1] = air_absorption[1]; params.airAbsorption[2] = air_absorption[2];
        }
        else {
            params.airAbsorption[0] = 1.0f; params.airAbsorption[1] = 1.0f; params.airAbsorption[2] = 1.0f;
        }

        if (apply_directivity) {
            params.flags = static_cast<IPLDirectEffectFlags>(params.flags | IPL_DIRECTEFFECTFLAGS_APPLYDIRECTIVITY);
            params.directivity = directivity_value;
        }
        else {
            params.directivity = 1.0f;
        }
        params.distanceAttenuation = attenuation;

        iplDirectEffectApply(direct_effect, &params, &internal_mono_buffer, &internal_mono_buffer);

        // 4. Calculate Vectors
        Vector3 v_l = ResonanceUtils::to_godot_vector3(listener_coords.origin);
        Vector3 v_s = ResonanceUtils::to_godot_vector3(source_pos);
        Vector3 vec = v_s - v_l;

        Vector3 ahead = ResonanceUtils::to_godot_vector3(listener_coords.ahead);
        Vector3 right = ResonanceUtils::to_godot_vector3(listener_coords.right);
        Vector3 up = ResonanceUtils::to_godot_vector3(listener_coords.up);

        float z = -vec.dot(ahead);
        float x = vec.dot(right);
        float y = vec.dot(up);

        IPLVector3 local_dir = { x, y, z };
        float len_sq = x * x + y * y + z * z;
        if (len_sq > 1e-8f) {
            float len = sqrt(len_sq);
            local_dir.x /= len; local_dir.y /= len; local_dir.z /= len;
        }
        else {
            local_dir = { 0.0f, 0.0f, -1.0f };
        }
        last_direction = local_dir;
        last_hrtf_bilinear = hrtf_interpolation_bilinear;
        last_spatial_blend = spatial_blend;
        last_use_ambisonics_encode_path = use_ambisonics_encode_path;

        apply_spatialization(local_dir, out_buffer, use_ambisonics_encode_path, use_binaural,
            hrtf_interpolation_bilinear, spatial_blend);
    }

    void ResonanceDirectProcessor::apply_spatialization(const IPLVector3& dir, IPLAudioBuffer& out,
        bool use_ambi_path, bool use_binaural, bool hrtf_bilinear, float spatial_blend) {
        if (use_ambi_path && ambisonics_encode_effect && ambisonics_binaural_effect && internal_ambi_buffer.data && use_binaural && hrtf_handle) {
            IPLAmbisonicsEncodeEffectParams encParams{};
            encParams.direction = dir;
            encParams.order = ambisonic_order;
            iplAmbisonicsEncodeEffectApply(ambisonics_encode_effect, &encParams, &internal_mono_buffer, &internal_ambi_buffer);

            IPLAmbisonicsBinauralEffectParams ambBinParams{};
            ambBinParams.hrtf = hrtf_handle;
            ambBinParams.order = ambisonic_order;
            iplAmbisonicsBinauralEffectApply(ambisonics_binaural_effect, &ambBinParams, &internal_ambi_buffer, &out);
        }
        else if (use_binaural && binaural_effect && hrtf_handle) {
            IPLBinauralEffectParams binParams{};
            binParams.direction = dir;
            binParams.interpolation = hrtf_bilinear ? IPL_HRTFINTERPOLATION_BILINEAR : IPL_HRTFINTERPOLATION_NEAREST;
            binParams.spatialBlend = spatial_blend;
            binParams.hrtf = hrtf_handle;
            binParams.peakDelays = nullptr;
            iplBinauralEffectApply(binaural_effect, &binParams, &internal_mono_buffer, &out);
        }
        else if (panning_effect) {
            IPLPanningEffectParams panParams{};
            panParams.direction = dir;
            iplPanningEffectApply(panning_effect, &panParams, &internal_mono_buffer, &out);
        }
    }

    bool ResonanceDirectProcessor::process_tail(IPLAudioBuffer& out_buffer) {
        if (!is_initialized || !direct_effect) {
            if (out_buffer.data) {
                for (int i = 0; i < out_buffer.numChannels; i++) {
                    if (out_buffer.data[i]) memset(out_buffer.data[i], 0, frame_size * sizeof(float));
                }
            }
            return false;
        }
        if (!out_buffer.data) return false;
        if (iplDirectEffectGetTailSize(direct_effect) <= 0) return false;

        IPLAudioEffectState state = iplDirectEffectGetTail(direct_effect, &internal_mono_buffer);
        if (state == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE) return false;

        apply_spatialization(last_direction, out_buffer, last_use_ambisonics_encode_path, true,
            last_hrtf_bilinear, last_spatial_blend);
        return true;
    }

    void ResonanceDirectProcessor::reset_for_new_playback() {
        if (direct_effect) iplDirectEffectReset(direct_effect);
    }
}
#include "resonance_processor_path.h"
#include "resonance_server.h"
#include "resonance_log.h"

namespace godot {

    ResonancePathProcessor::ResonancePathProcessor() {}

    ResonancePathProcessor::~ResonancePathProcessor() { cleanup(); }

    void ResonancePathProcessor::initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order) {
        if (is_initialized) return;
        if (!p_context) {
            ResonanceLog::error("PathProcessor: Context is null!");
            return;
        }

        context = p_context;
        frame_size = p_frame_size;
        ambisonic_order = p_ambisonic_order;

        IPLAudioSettings audioSettings{};
        audioSettings.samplingRate = p_sample_rate;
        audioSettings.frameSize = frame_size;

        ResonanceServer* srv = ResonanceServer::get_singleton();
        IPLHRTF hrtf = (srv && srv->is_initialized()) ? srv->get_hrtf_handle() : nullptr;

        IPLPathEffectSettings pathSettings{};
        pathSettings.maxOrder = ambisonic_order;
        pathSettings.spatialize = IPL_TRUE;
        pathSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
        pathSettings.speakerLayout.numSpeakers = 2;
        pathSettings.hrtf = hrtf;

        if (iplPathEffectCreate(context, &audioSettings, &pathSettings, &path_effect) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("PathProcessor: Failed to create IPLPathEffect");
            return;
        }

        iplAudioBufferAllocate(context, 1, frame_size, &internal_mono_buffer);
        iplAudioBufferAllocate(context, 2, frame_size, &internal_stereo_buffer);

        if (!internal_mono_buffer.data || !internal_stereo_buffer.data) {
            ResonanceLog::error("PathProcessor: Buffer allocation failed.");
            cleanup();
            return;
        }

        is_initialized = true;
        ResonanceLog::info("PathProcessor initialized successfully.");
    }

    void ResonancePathProcessor::cleanup() {
        if (path_effect) { iplPathEffectRelease(&path_effect); path_effect = nullptr; }
        if (context) {
            if (internal_mono_buffer.data) iplAudioBufferFree(context, &internal_mono_buffer);
            if (internal_stereo_buffer.data) iplAudioBufferFree(context, &internal_stereo_buffer);
        }
        memset(&internal_mono_buffer, 0, sizeof(internal_mono_buffer));
        memset(&internal_stereo_buffer, 0, sizeof(internal_stereo_buffer));
        context = nullptr;
        is_initialized = false;
    }

    void ResonancePathProcessor::process(const IPLAudioBuffer& in_buffer, const IPLPathEffectParams& params, IPLAudioBuffer& out_buffer) {
        if (!is_initialized || !path_effect || !params.shCoeffs) return;

        // IPL API has non-const param; input is read-only
        iplAudioBufferDownmix(context, const_cast<IPLAudioBuffer*>(&in_buffer), &internal_mono_buffer);

        IPLPathEffectParams effective_params = params;
        effective_params.hrtf = params.hrtf;

        iplPathEffectApply(path_effect, &effective_params, &internal_mono_buffer, &out_buffer);
    }

} // namespace godot

#include "resonance_processor_path.h"
#include "resonance_log.h"
#include "resonance_server.h"
#include <algorithm>
#include <cstring>

namespace godot {

ResonancePathProcessor::~ResonancePathProcessor() { cleanup(); }

void ResonancePathProcessor::initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order) {
    if (init_flags != PathInitFlags::NONE)
        return;
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
    init_flags = init_flags | PathInitFlags::PATHEFFECT;

    if (iplAudioBufferAllocate(context, 1, frame_size, &internal_mono_buffer) != IPL_STATUS_SUCCESS ||
        !internal_mono_buffer.data) {
        ResonanceLog::error("PathProcessor: Buffer allocation failed.");
        cleanup();
        return;
    }
    init_flags = init_flags | PathInitFlags::BUFFERS;
    ResonanceLog::info("PathProcessor initialized successfully.");
}

void ResonancePathProcessor::cleanup() {
    if (path_effect) {
        iplPathEffectRelease(&path_effect);
        path_effect = nullptr;
    }
    if (context && internal_mono_buffer.data) {
        iplAudioBufferFree(context, &internal_mono_buffer);
    }
    memset(&internal_mono_buffer, 0, sizeof(internal_mono_buffer));
    context = nullptr;
    init_flags = PathInitFlags::NONE;
}

void ResonancePathProcessor::process(const IPLAudioBuffer& in_buffer, const IPLPathEffectParams& params, IPLAudioBuffer& out_buffer) {
    if (!(init_flags & PathInitFlags::PATHEFFECT) || !(init_flags & PathInitFlags::BUFFERS) || !path_effect || !params.shCoeffs)
        return;
    if (!in_buffer.data || !out_buffer.data || out_buffer.numSamples < frame_size)
        return;

    // IPL API has non-const param; input is read-only
    iplAudioBufferDownmix(context, const_cast<IPLAudioBuffer*>(&in_buffer), &internal_mono_buffer);

    IPLPathEffectParams effective_params = params;
    for (int i = 0; i < IPL_NUM_BANDS; i++) {
        effective_params.eqCoeffs[i] = std::max(resonance::kPathEQCoeffMin, std::min(resonance::kPathEQCoeffMax, params.eqCoeffs[i]));
    }

    iplPathEffectApply(path_effect, &effective_params, &internal_mono_buffer, &out_buffer);
}

} // namespace godot

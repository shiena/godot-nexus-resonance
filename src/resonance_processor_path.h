#ifndef RESONANCE_PROCESSOR_PATH_H
#define RESONANCE_PROCESSOR_PATH_H

#include "resonance_constants.h"
#include <phonon.h>

namespace godot {

    enum class PathInitFlags : int {
        NONE = 0,
        PATHEFFECT = 1 << 0,
        BUFFERS = 1 << 1,
    };
    inline PathInitFlags operator|(PathInitFlags a, PathInitFlags b) {
        return static_cast<PathInitFlags>(static_cast<int>(a) | static_cast<int>(b));
    }
    inline bool operator&(PathInitFlags a, PathInitFlags b) { return (static_cast<int>(a) & static_cast<int>(b)) != 0; }

    class ResonancePathProcessor {
    private:
        IPLContext context = nullptr;
        IPLPathEffect path_effect = nullptr;
        IPLAudioBuffer internal_mono_buffer{};

        PathInitFlags init_flags = PathInitFlags::NONE;
        int frame_size = resonance::kGodotDefaultFrameSize;
        int ambisonic_order = 1;

    public:
        ResonancePathProcessor() = default;
        ~ResonancePathProcessor();

        ResonancePathProcessor(const ResonancePathProcessor&) = delete;
        ResonancePathProcessor& operator=(const ResonancePathProcessor&) = delete;
        ResonancePathProcessor(ResonancePathProcessor&&) = delete;
        ResonancePathProcessor& operator=(ResonancePathProcessor&&) = delete;

        void initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_ambisonic_order);
        void cleanup();

        void process(const IPLAudioBuffer& in_buffer, const IPLPathEffectParams& params, IPLAudioBuffer& out_buffer);
    };

} // namespace godot

#endif

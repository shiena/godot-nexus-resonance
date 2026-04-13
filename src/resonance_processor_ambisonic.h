#ifndef RESONANCE_PROCESSOR_AMBISONIC_H
#define RESONANCE_PROCESSOR_AMBISONIC_H

#include "resonance_constants.h"
#include <cstddef>
#include <phonon.h>
#include <vector>

namespace godot {

enum class AmbisonicInitFlags : int {
    NONE = 0,
    ROTATION = 1 << 0,
    DECODE = 1 << 1,
    BUFFERS = 1 << 2,
};
inline AmbisonicInitFlags operator|(AmbisonicInitFlags a, AmbisonicInitFlags b) {
    return static_cast<AmbisonicInitFlags>(static_cast<int>(a) | static_cast<int>(b));
}
inline bool operator&(AmbisonicInitFlags a, AmbisonicInitFlags b) { return (static_cast<int>(a) & static_cast<int>(b)) != 0; }

class ResonanceAmbisonicProcessor {
  private:
    IPLContext context = nullptr;
    IPLAmbisonicsRotationEffect rotation_effect = nullptr;
    IPLAmbisonicsDecodeEffect ambisonics_dec_effect = nullptr;

    // Buffers
    IPLAudioBuffer sa_in_buffer{};      // (order+1)^2 Channels (Ambisonic B-Format)
    IPLAudioBuffer sa_rotated_buffer{}; // Rotated Ambisonics (listener-space)

    AmbisonicInitFlags init_flags = AmbisonicInitFlags::NONE;
    int frame_size = resonance::kGodotDefaultFrameSize;
    int sample_rate = 48000;
    int ambisonic_order = 1;
    bool rotation_enabled = true;

  public:
    ResonanceAmbisonicProcessor() = default;
    ~ResonanceAmbisonicProcessor();

    ResonanceAmbisonicProcessor(const ResonanceAmbisonicProcessor&) = delete;
    ResonanceAmbisonicProcessor& operator=(const ResonanceAmbisonicProcessor&) = delete;
    ResonanceAmbisonicProcessor(ResonanceAmbisonicProcessor&&) = delete;
    ResonanceAmbisonicProcessor& operator=(ResonanceAmbisonicProcessor&&) = delete;

    void initialize(IPLContext p_context, int p_sample_rate, int p_frame_size, int p_order, bool p_rotation_enabled = true);
    void cleanup();

    // Process N-channel Ambisonic input (N=(order+1)^2) -> 2-channel output based on orientation
    void process(const float* input_data, size_t sample_count, IPLAudioBuffer& out_buffer,
                 const IPLCoordinateSpace3& listener_orient);
    void process(const std::vector<float>& input_data, IPLAudioBuffer& out_buffer,
                 const IPLCoordinateSpace3& listener_orient);
};

} // namespace godot

#endif
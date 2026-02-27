#ifndef RESONANCE_CONSTANTS_H
#define RESONANCE_CONSTANTS_H

namespace resonance {

/// Version string (centralized; override via NEXUS_RESONANCE_VERSION when building)
#ifndef NEXUS_RESONANCE_VERSION
#define NEXUS_RESONANCE_VERSION "5.15"
#endif
constexpr const char* kVersion = NEXUS_RESONANCE_VERSION;

/// Must match Godot default audio frame size for Steam Audio
constexpr int kGodotDefaultFrameSize = 512;
/// Max supported frame size (for stack buffers; 2048 = lowest CPU, highest latency)
constexpr int kMaxAudioFrameSize = 2048;

/// Ring buffer capacity for audio playback (ResonancePlayer, ResonanceAmbisonicPlayer)
constexpr int kRingBufferCapacity = 8192;

/// Default reverb/IR duration in seconds (used for irSize = sample_rate * duration)
constexpr float kDefaultReverbDurationSec = 2.0f;

/// Baker defaults for probe reflections bake
constexpr float kBakerSimulatedDuration = 2.0f;
constexpr float kBakerIrradianceMinDistance = 0.5f;

/// Baker default parameters (overridable via ProjectSettings audio/nexus_resonance/bake_*)
constexpr int kBakeDefaultNumRays = 4096;
constexpr int kBakeDefaultNumBounces = 4;
constexpr int kBakeDefaultNumThreads = 2;
constexpr float kBakePathingDefaultVisRange = 500.0f;
constexpr float kBakePathingDefaultPathRange = 100.0f;
constexpr int kBakePathingDefaultNumSamples = 16;
constexpr float kBakePathingDefaultRadius = 0.5f;
constexpr float kBakePathingDefaultThreshold = 0.1f;

/// Simulator shared inputs (duration and irradianceMinDistance)
constexpr float kSimulatorSharedInputsDuration = 2.0f;
constexpr float kSimulatorIrradianceMinDistance = 0.1f;

/// Ray debug visualization max distance for reflection ray tracing
constexpr float kRayDebugMaxDistance = 500.0f;

/// Ticks to skip RunPathing after SEH crash (reduces exception storm)
constexpr int kPathingCrashCooldownTicks = 5;

} // namespace resonance

#endif // RESONANCE_CONSTANTS_H

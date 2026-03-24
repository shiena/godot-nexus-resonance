#ifndef RESONANCE_PLAYER_H
#define RESONANCE_PLAYER_H

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/templates/safe_refcount.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <atomic>
#include <chrono>
#include <limits>
#include <phonon.h>
#include <vector>

#include "resonance_constants.h"
#include "resonance_debug_drawer.h"
#include "resonance_mixer_processor.h"
#include "resonance_processor_direct.h"
#include "resonance_processor_path.h"
#include "resonance_processor_reflection.h"
#include "resonance_ring_buffer.h"
#include "resonance_server.h"

namespace godot {
struct PlaybackParameters {
    int32_t source_handle = -1;              // Source handle for Steam Audio
    float occlusion = 0.0f;                  // 0=Visible, 1=Blocked
    float transmission[3] = {1, 1, 1};       // Wall transparency
    float attenuation = 1.0f;                // Distance attenuation factor (direct, reverb, pathing)
    float reverb_pathing_attenuation = 1.0f; // Attenuation for reverb/pathing; capped to inverse-level when curve/linear (prevents overdrive)
    float distance = 0.0f;                   // Source-listener distance (meters), for reverb falloff
    bool enable_direct = true;               // Enable direct sound processing
    bool enable_reverb = true;               // Enable reverb processing
    bool has_valid_reverb = false;           // Whether the reverb parameters are valid

    Vector3 source_position = Vector3(0, 0, 0); // Source position in world space
    bool use_binaural = true;                   // Use HRTF binaural processing
    float spatial_blend = 1.0f;                 // 0=unspatialized, 1=fully spatialized (e.g. for diegetic/non-diegetic blend)
    bool use_ambisonics_encode = false;         // Encode point source to Ambisonics before binaural (for mixing scenarios)

    bool apply_air_absorption = false;            // Enable air absorption effect
    float air_absorption[3] = {1.0f, 1.0f, 1.0f}; // Air absorption coefficients (Low, Mid, High)

    bool apply_directivity = false; // Enable directivity effect
    float directivity_value = 1.0f; // Directivity factor (0.0 = Omni, 1.0 = Dipole)

    IPLCoordinateSpace3 listener_orientation{};

    // Per-source mix levels (0.0 = mute, 1.0 = full). SteamAudio-style Direct/Reflections/Pathing Mix Level.
    float direct_mix_level = 1.0f;
    float reflections_mix_level = 1.0f;
    float pathing_mix_level = 1.0f;
    bool apply_hrtf_to_reflections = true;
    bool apply_hrtf_to_pathing = true;

    // Per-source hybrid reverb overrides. Only applied when reflection_type is Hybrid. -1 = use simulation value.
    float reflections_eq[3] = {1.0f, 1.0f, 1.0f}; // EQ multipliers for parametric part (1.0 = no change)
    int reflections_delay = -1;                   // Samples before parametric starts; -1 = use simulation

    // Parametric/Hybrid split output: when true, reverb goes to reverb ring instead of main output.
    bool reverb_split_output = false;
};

class ResonanceInternalPlayback : public AudioStreamPlayback {
    GDCLASS(ResonanceInternalPlayback, AudioStreamPlayback)

  private:
    static const int kMaxBlocksPerMixCall = 4;           // Cap blocks per _mix to avoid exceeding audio callback budget (Godot frames 1024 + frame_size 512)
    int frame_size_ = resonance::kGodotDefaultFrameSize; // Steam Audio block size from ResonanceServer (256/512/1024)
    Ref<AudioStreamPlayback> base_playback;

    std::atomic<bool> params_dirty = false;
    PlaybackParameters params_next;
    PlaybackParameters params_current;

    bool is_initialized = false;
    int current_sample_rate = 44100;

    IPLSource local_source = nullptr;
    int32_t current_source_handle = -1;
    IPLContext context = nullptr;

    // --- MODULAR PROCESSORS ---
    ResonanceDirectProcessor direct_processor;
    ResonanceReflectionProcessor reflection_processor;
    ResonancePathProcessor path_processor;
    ResonanceMixerProcessor mixer_processor;

    // Main Buffers (Bridge between Godot & Processors)
    IPLAudioBuffer sa_in_buffer{};
    IPLAudioBuffer sa_direct_out_buffer{}; // Output from Direct Processor
    IPLAudioBuffer sa_path_out_buffer{};   // Output from Path Processor
    IPLAudioBuffer sa_final_mix_buffer{};  // Mixed result

    RingBuffer<float> input_ring_l;
    RingBuffer<float> input_ring_r;
    RingBuffer<float> output_ring_l;
    RingBuffer<float> output_ring_r;
    RingBuffer<float> output_ring_reverb_l;
    RingBuffer<float> output_ring_reverb_r;

    // Temporary linear buffer to hold exactly one Steam Audio frame (1024) for processing
    std::vector<float> temp_process_buffer_l;
    std::vector<float> temp_process_buffer_r;

    // Reusable buffers for read_reverb_frames (avoids allocation in audio path)
    std::vector<float> temp_reverb_buffer_l;
    std::vector<float> temp_reverb_buffer_r;

    // Volume Ramping State
    float prev_direct_weight = 1.0f;
    float prev_conv_reverb_gain = -1.0f; // For Convolution mixer feed ramp; -1 = no ramp on first use

    // Throttle "no reverb params" warning: skip first 3, then log only every 200+ misses (reset after log)
    int no_reverb_warn_count = 0;

    // Input-start detection: delay processing until first non-zero sample
    // to avoid ramp artifacts when Godot sends incorrect params before playback actually starts.
    bool input_started = false;

    // Parametric pathing fallback: persistent sh coeffs when baked path fails (order 1 = 4 coeffs)
    float parametric_path_sh_coeffs[4];

    // --- AUDIO INSTRUMENTATION (for dropout debugging) ---
    // Atomic counters updated from audio thread; read from main thread
    std::atomic<uint64_t> instrumentation_input_dropped{0};                                   // Samples dropped when input ring full
    std::atomic<uint64_t> instrumentation_output_underrun{0};                                 // Output frames filled with silence
    std::atomic<uint64_t> instrumentation_output_blocked{0};                                  // Processing skipped (output ring full)
    std::atomic<uint64_t> instrumentation_mix_call_count{0};                                  // Total _mix calls
    std::atomic<uint64_t> instrumentation_blocks_processed{0};                                // Blocks processed (512 frames each)
    std::atomic<uint64_t> instrumentation_passthrough_blocks{0};                              // Blocks in passthrough (no local_source)
    std::atomic<uint64_t> instrumentation_reverb_miss_blocks{0};                              // Wanted reverb but fetch_reverb_params=false
    std::atomic<uint64_t> instrumentation_max_block_time_us{0};                               // Max _process_steam_audio_block duration (us)
    std::atomic<uint64_t> instrumentation_late_mix_count{0};                                  // _mix calls with inter-callback time >15ms
    std::atomic<uint64_t> instrumentation_param_sync_count{0};                                // Times params were synced (params_dirty)
    std::atomic<uint64_t> instrumentation_zero_input_count{0};                                // _mix calls with samples_read==0 (tail drain)
    std::atomic<int32_t> instrumentation_mix_frames_min{std::numeric_limits<int32_t>::max()}; // Min samples_read per _mix (when >0)
    std::atomic<int32_t> instrumentation_mix_frames_max{0};                                   // Max samples_read per _mix
    std::atomic<uint64_t> instrumentation_silent_output_blocks{0};                            // Processed blocks with output RMS < 0.0001
    std::atomic<uint32_t> instrumentation_last_output_rms_q8{0};                              // Last block output RMS * 256 (fixed-point for display)
    std::atomic<float> debug_signal_direct{0.0f};                                             // Effective direct gain (for Debug Sources display)
    std::atomic<float> debug_signal_reverb{0.0f};                                             // Effective reverb gain (for Debug Sources display)
    std::atomic<float> debug_signal_pathing{0.0f};                                            // Effective pathing gain (for Debug Sources display)
    std::chrono::steady_clock::time_point last_mix_time_;                                     // For inter-callback timing (audio thread only)

    void _lazy_init_steam_audio(int sampling_rate);                                            // Lazy init to avoid overhead if not needed
    void _cleanup_steam_audio();                                                               // Cleanup all resources
    void _process_steam_audio_block();                                                         // Process a single block of audio through Steam Audio
    void _sync_params();                                                                       // Sync parameters from next to current
    void _add_reverb_to_output(IPLAudioBuffer* reverb_buf, float refl_mix, bool split_output); // Parametric vs Convolution, split vs mix

  public:
    ResonanceInternalPlayback();
    ~ResonanceInternalPlayback();

    /// Called by ResonanceServer before iplContextRelease (userdata = this).
    static void ipl_context_reinit_cleanup(void* userdata);

    ResonanceInternalPlayback(const ResonanceInternalPlayback&) = delete;
    ResonanceInternalPlayback(ResonanceInternalPlayback&&) = delete;

    void set_base_playback(const Ref<AudioStreamPlayback>& p_playback);
    void update_parameters(const PlaybackParameters& p_params);

    virtual int32_t _mix(AudioFrame* buffer, double rate_scale, int32_t frames); // Mixes audio frames into the buffer
    virtual void _start(double from_pos) override;                               // Starts playback from a specific position

    /// Debug Sources: effective D/R/P gains from last audio block. Safe to call from main thread.
    void get_debug_signal_levels(float& out_direct, float& out_reverb, float& out_pathing) const;
    /// Snapshot of instrumentation counters for debugging dropouts. Safe to call from main thread.
    void get_instrumentation_snapshot(uint64_t& out_input_dropped, uint64_t& out_output_underrun,
                                      uint64_t& out_output_blocked, uint64_t& out_mix_calls, uint64_t& out_blocks_processed,
                                      uint64_t& out_passthrough_blocks, uint64_t& out_reverb_miss_blocks, uint64_t& out_max_block_time_us,
                                      uint64_t& out_late_mix, uint64_t& out_param_syncs, uint64_t& out_zero_input,
                                      int32_t& out_mix_frames_min, int32_t& out_mix_frames_max,
                                      uint64_t& out_silent_blocks, float& out_last_rms) const;
    /// Reset all instrumentation counters. Call from main thread to clear and re-observe.
    void reset_instrumentation();
    /// Fills buffer with reverb frames (or silence if unavailable). Always returns frames. Called from reverb playback _mix.
    int32_t read_reverb_frames(AudioFrame* buffer, int32_t frames);
    virtual void _stop() override;                // Stops playback
    virtual bool _is_playing() const override;    // Checks if playback is active
    virtual int _get_loop_count() const override; // Returns the loop count for the playback
    virtual void _seek(double position) override; // Seeks to a specific position in the stream

  protected:
    static void _bind_methods() {}
};

class ResonanceInternalStream : public AudioStream {
    GDCLASS(ResonanceInternalStream, AudioStream)
  private:
    Ref<AudioStream> base_stream;

  public:
    void set_base_stream(const Ref<AudioStream>& p_stream);
    virtual Ref<AudioStreamPlayback> _instantiate_playback() const override;
    virtual String _get_stream_name() const override { return "ResonanceInternal"; }
    virtual double _get_length() const override { return base_stream.is_valid() ? base_stream->get_length() : 0.0; }
    virtual bool _is_monophonic() const override { return false; }

  protected:
    static void _bind_methods() {}
};

class ResonancePlayer;

class ResonanceReverbPlayback : public AudioStreamPlayback {
    GDCLASS(ResonanceReverbPlayback, AudioStreamPlayback)
  private:
    ResonancePlayer* parent_player = nullptr;

  public:
    ResonanceReverbPlayback() = default;
    ResonanceReverbPlayback(const ResonanceReverbPlayback&) = delete;
    ResonanceReverbPlayback(ResonanceReverbPlayback&&) = delete;
    void set_parent_player(ResonancePlayer* p_player);
    virtual int32_t _mix(AudioFrame* buffer, float rate_scale, int32_t frames) override;
    virtual void _start(double from_pos) override;
    virtual void _stop() override;
    virtual bool _is_playing() const override;
    virtual int _get_loop_count() const override;
    virtual void _seek(double position) override;

  protected:
    static void _bind_methods();
};

class ResonanceReverbStream : public AudioStream {
    GDCLASS(ResonanceReverbStream, AudioStream)
  private:
    ResonancePlayer* parent_player = nullptr;

  public:
    ResonanceReverbStream() = default;
    ResonanceReverbStream(const ResonanceReverbStream&) = delete;
    ResonanceReverbStream(ResonanceReverbStream&&) = delete;
    void set_parent_player(ResonancePlayer* p_player);
    virtual Ref<AudioStreamPlayback> _instantiate_playback() const override;
    virtual String _get_stream_name() const override { return "ResonanceReverb"; }
    virtual double _get_length() const override { return 0.0; }
    virtual bool _is_monophonic() const override { return false; }

  protected:
    static void _bind_methods();
};

class ResonancePlayer : public AudioStreamPlayer3D {
    GDCLASS(ResonancePlayer, AudioStreamPlayer3D)
  public:
    enum AttenuationMode {
        ATTENUATION_INVERSE,     // Physics based (1/dist)
        ATTENUATION_LINEAR,      // Linear falloff to 0 at max_dist
        ATTENUATION_CUSTOM_CURVE // User defined curve
    };

  private:
    Ref<ResonanceInternalStream> internal_stream;
    int32_t source_handle = -1;
    NodePath pathing_probe_volume; // Scene-specific: which ProbeVolume to use for pathing
    Ref<Resource> player_config;   // ResonancePlayerConfig; null = behave as plain AudioStreamPlayer3D

    // Config cache: refresh every N frames to reduce _config_* overhead while still reflecting edits
    static const int kConfigCacheRefreshInterval = 15;
    int config_cache_frame_countdown = 0;
    struct ConfigCache {
        float min_distance, max_distance, source_radius;
        int attenuation_mode;
        Ref<Curve> attenuation_curve;
        bool air_absorption_enabled;
        int air_absorption_input;
        float air_absorption_low, air_absorption_mid, air_absorption_high;
        int direct_binaural_override;
        bool directivity_enabled;
        float directivity_weight, directivity_power, spatial_blend;
        bool use_ambisonics_encode;
        bool path_validation_enabled, find_alternate_paths;
        int reflections_type, reflections_enabled, pathing_enabled_override;
        int apply_hrtf_to_reflections_override, apply_hrtf_to_pathing_override;
        int occlusion_input, transmission_input, directivity_input;
        float occlusion_value, transmission_low, transmission_mid, transmission_high, directivity_value;
        int occlusion_samples, max_transmission_surfaces;
        float direct_mix_level, reflections_mix_level, pathing_mix_level;
        float reflections_eq_low, reflections_eq_mid, reflections_eq_high;
        int reflections_delay;
        int perspective_override;
        float perspective_factor;
    } config_cache_;
    bool config_cache_valid_ = false;

    // Debug visualization
    ResonanceDebugDrawer debug_drawer;
    ResonanceDebugData debug_overlay_last_data_{};
    bool debug_overlay_has_last_data_ = false;
    double debug_overlay_grace_timer_ = 0.0;

    void _update_stream_setup();                                 // Ensures the internal stream is set up correctly
    void _ensure_source_exists();                                // Ensures the source handle exists in the ResonanceServer
    void _ensure_config_valid();                                 // Refreshes config cache if countdown expired or invalid
    void _ensure_config_and_apply_source(int32_t pathing_batch); // Ensures config valid, then applies update_source (DRY for _process and clear_pathing_probe_immediate)
    void _apply_update_source(int32_t pathing_batch);

    void _setup_attenuation(ResonanceServer* srv);
    void _process_config_and_pathing(ResonanceServer* srv);
    /// Pushes occlusion/attenuation from simulation into the audio thread (same as one _process tick without debug UI).
    /// When opt_debug_out is set, fills debug fields (not signal levels).
    void _push_playback_parameters_from_simulation(ResonanceServer* srv, ResonanceDebugData* opt_debug_out = nullptr);
    void _deferred_push_playback_parameters();
    void _sync_player_debug_drawer(double delta, ResonanceServer* srv, const ResonanceDebugData& dbg_data, bool hud_active);
    void _compute_listener_data(Viewport* vp, Vector3& out_listener_pos, IPLCoordinateSpace3& out_listener_orient);
    void _compute_attenuation(float dist, const OcclusionData& occ_data, float& out_attenuation, float& out_reverb_pathing_attenuation);
    Vector3 _apply_perspective_correction(Vector3 listener_pos, Viewport* vp, bool apply_perspective, float perspective_factor_val);
    PlaybackParameters _build_playback_params(const Vector3& listener_pos, const IPLCoordinateSpace3& listener_orient,
                                              float attenuation, float reverb_pathing_attenuation, float dist, const Vector3& effective_source_pos,
                                              float occ_val, float tx_low, float tx_mid, float tx_high, float directivity_val, const Vector3& air_abs,
                                              bool has_reverb, bool direct_enabled, bool reverb_enabled);

    ResonanceInternalPlayback* _get_resonance_playback();
    void _update_reverb_split_child(const StringName& p_reverb_bus = StringName());

    bool reverb_split_output_ = false;
    bool exclude_from_debug_ = false;

    float _config_float(const char* key, float default_val) const;
    int _config_int(const char* key, int default_val) const;
    bool _config_bool(const char* key, bool default_val) const;
    Ref<Curve> _config_curve(const char* key, const Ref<Curve>& default_val) const;
    NodePath _config_node_path(const char* key) const;
    void _refresh_config_cache();

  protected:
    static void _bind_methods();

  public:
    ResonancePlayer() = default;
    ~ResonancePlayer() = default;

    void _ready() override;
    void _process(double delta) override;
    void _exit_tree() override;
    void play_stream(double from_position = 0.0);
    void stop();

    void set_pathing_probe_volume(const NodePath& p_path);
    NodePath get_pathing_probe_volume() const;
    /// Called by ResonanceProbeVolume when it is removed; immediately clears pathing batch so worker does not use freed data.
    void clear_pathing_probe_immediate();
    void set_player_config(const Ref<Resource>& p_config);
    Ref<Resource> get_player_config() const;
    void set_reverb_split_output(bool p_enable, const StringName& p_reverb_bus = StringName());
    bool get_reverb_split_output() const { return reverb_split_output_; }

    void set_exclude_from_debug(bool p_exclude);
    bool get_exclude_from_debug() const { return exclude_from_debug_; }

    /// Returns audio instrumentation dict for dropout debugging. Keys: input_dropped, output_underrun, output_blocked, mix_calls, blocks_processed. Empty when no player_config.
    Dictionary get_audio_instrumentation();
    /// Reset instrumentation counters on this player's playback. Call to clear and re-observe dropouts.
    void reset_audio_instrumentation();
};
} // namespace godot

// Register Enum for Godot Inspector
VARIANT_ENUM_CAST(ResonancePlayer::AttenuationMode);

#endif
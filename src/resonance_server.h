#ifndef RESONANCE_SERVER_H
#define RESONANCE_SERVER_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <phonon.h>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <cstdint>

#include "resonance_utils.h"
#include "resonance_geometry_asset.h"
#include "resonance_sofa_asset.h"
#include "resonance_probe_data.h"
#include "resonance_baker.h"
#include "resonance_server_config.h"
#include "resonance_steam_audio_context.h"
#include "resonance_probe_batch_registry.h"
#include "resonance_scene_manager.h"
#include "handle_manager.h"
#include "ray_trace_debug_context.h"

namespace godot {

    class ResonanceGeometry;

    // --- DATA STRUCTURES ---
    struct OcclusionData {
        float occlusion;
        float transmission[3];
        float air_absorption[3];     // From simulation when IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION enabled
        float directivity;           // From simulation when IPL_DIRECTSIMULATIONFLAGS_DIRECTIVITY enabled
        float distance_attenuation;  // From simulation when distanceAttenuationModel set
    };

    // --- SOURCE MANAGER ---
    class SourceManager : public HandleManagerBase<IPLSource, _handle_release_source> {
    public:
        SourceManager();
        ~SourceManager();
        int32_t add_source(IPLSource source);
        void remove_source(int32_t handle);
        IPLSource get_source(int32_t handle);
        void get_all_handles(std::vector<int32_t>& out);
    };

    // --- RESONANCE SERVER ---
    class ResonanceServer : public Object {
        GDCLASS(ResonanceServer, Object)

    public:
        /// Used by distance attenuation callback; layout must match usage in resonance_server.cpp
        struct AttenuationCallbackData {
            int mode = 0;  // 0=inverse (unused), 1=linear, 2=curve
            float min_distance = 1.0f;
            float max_distance = 500.0f;
            float curve_samples[64] = {};  // For curve mode: normalized dist 0..1 -> attenuation
            int num_curve_samples = 64;
        };

    private:
        // Steam Audio Context (owns context, embree, opencl, radeon rays, TAN, HRTF)
        std::unique_ptr<ResonanceSteamAudioContext> steam_audio_context_;
        IPLScene scene = nullptr;
        std::vector<IPLStaticMesh> _runtime_static_meshes;  // Loaded from ResonanceStaticScene assets (additive); released on shutdown
        int _runtime_static_triangle_count = 0;  // Sum of triangles in _runtime_static_meshes
        std::vector<int> _runtime_static_debug_mesh_ids;  // Debug viz mesh IDs for static scenes (unregister on clear)
        std::unordered_map<int32_t, AttenuationCallbackData> _source_attenuation_callback_data;
        std::mutex _attenuation_callback_mutex;
        IPLSimulator simulator = nullptr;

        // Mixer (processing done in ResonanceMixerProcessor; only mixer handle needed)
        IPLReflectionMixer reflection_mixer = nullptr;
        std::mutex mixer_access_mutex;

        // Reverb Bus instrumentation (updated from audio thread; read from main)
        std::atomic<uint64_t> reverb_effect_process_calls{0};
        std::atomic<uint64_t> reverb_effect_mixer_null{0};
        std::atomic<uint64_t> reverb_effect_success{0};
        std::atomic<uint64_t> reverb_effect_frames_written{0};
        std::atomic<float> reverb_effect_output_peak{0.0f};
        std::atomic<uint64_t> reverb_mixer_feed_count{0};
        /// Convolution debug: fetch_reverb_params returned true with valid IR (reflection_type==0)
        std::atomic<uint64_t> reverb_convolution_valid_fetches{0};
        /// Convolution debug: process_mix called for convolution with ir==null (should not happen)
        std::atomic<uint64_t> reverb_convolution_feed_ir_null{0};
        /// Convolution debug: min reverb_gain seen when feeding (attenuation * transmission * air_abs)
        std::atomic<float> reverb_convolution_gain_min{1.0f};
        /// Convolution debug: max reverb_gain seen when feeding
        std::atomic<float> reverb_convolution_gain_max{0.0f};
        /// Convolution debug: max RMS of mono input to convolution (before reverb_gain scale)
        std::atomic<float> reverb_convolution_input_rms_max{0.0f};

        // Helpers
        ResonanceBaker baker;
        SourceManager source_manager;
        ResonanceServerConfig config_;

        // Simulation State
        IPLSimulationSettings simulation_settings{};
        int global_triangle_count = 0;

        // Configuration (Defaults)
        int current_sample_rate = 48000;
        int frame_size = 512;  // Steam Audio block size (256/512/1024). Matched to Godot mix callback for best perf.
        int ambisonic_order = 1;
        float max_reverb_duration = 2.0f;
        int simulation_threads = 1;  // Computed from simulation_cpu_cores_percent
        float simulation_cpu_cores_percent = 0.05f;  // 0-1 fraction of CPU cores for raytracing
        int max_rays = 4096;
        int max_bounces = 4;
        float reverb_influence_radius = 10000.0f;
        float reverb_max_distance = 0.0f;  // Extra reverb falloff: 0 = use attenuation only; >0 = fade reverb by this distance
        float reverb_transmission_amount = 1.0f;  // 0 = no transmission damping on reverb, 1 = full damping

        // Reflection type: 0 = Convolution, 1 = Parametric, 2 = Hybrid
        int reflection_type = 0;
        float hybrid_reverb_transition_time = 1.0f;
        float hybrid_reverb_overlap_percent = 0.25f;  // 0.0-1.0 (25% from config)
        // Transmission type: 0 = FreqIndependent, 1 = FreqDependent
        int transmission_type = 0;
        // Occlusion type: 0 = Raycast, 1 = Volumetric
        int occlusion_type = 1;
        // Custom HRTF: ResonanceSOFAAsset with volume/norm. Null = default embedded HRTF.
        Ref<ResonanceSOFAAsset> hrtf_sofa_asset;
        // Use HRTF for reverb Ambisonic decode (better spatialization)
        bool reverb_binaural = true;
        // HRTF interpolation: false = Nearest (faster), true = Bilinear (smoother for moving sources)
        bool hrtf_interpolation_bilinear = false;
        // Virtual Surround: decode reverb Ambisonics to 7.1, then iplVirtualSurroundEffect -> stereo (for speaker layouts without HRTF)
        bool use_virtual_surround = false;
        // Enable pathing simulation (multi-path sound propagation around obstacles)
        bool pathing_enabled = false;
        // Pathing visibility params (bakingVisibilityRadius/Threshold/Range)
        float pathing_vis_radius = 0.5f;
        float pathing_vis_threshold = 0.1f;
        float pathing_vis_range = 100.0f;
        bool pathing_normalize_eq = true;
        // Optional custom deviation model for pathing (freq-dependent attenuation around corners). nullptr = default (UTD).
        IPLDeviationModel _pathing_deviation_model{};
        bool _pathing_deviation_callback_enabled = false;
        std::mutex _pathing_deviation_mutex;
        // Use Radeon Rays (OpenCL GPU) for ray tracing when available. Falls back to Embree if unavailable.
        bool use_radeon_rays = false;
        // OpenCL device selection when use_radeon_rays: type 0=GPU, 1=CPU, 2=Any; index = device index in list
        int opencl_device_type = 0;   // IPL_OPENCLDEVICETYPE_GPU
        int opencl_device_index = 0;
        // Context: validation (debug), SIMD level (0=AVX512, 1=AVX2, 2=AVX, 3=SSE4, 4=SSE2; -1=default)
        bool context_validation = false;
        int context_simd_level = -1;  // -1 = use phonon default (auto)
        // Realtime reflection quality (when max_rays > 0)
        float realtime_irradiance_min_distance = 0.1f;
        float realtime_simulation_duration = 2.0f;
        int realtime_num_diffuse_samples = 32;

        // Flags
        bool output_direct_enabled = true;
        bool output_reverb_enabled = true;
        bool debug_occlusion = false;
        bool debug_reflections = false;
        bool debug_pathing = false;

        // Perspective Correction (non-VR: spatialize from on-screen position for better localization)
        bool perspective_correction_enabled = false;
        float perspective_correction_factor = 1.0f;

        // Pathing visualization (callback stores segments for debug drawing)
        struct PathVisSegment {
            Vector3 from;
            Vector3 to;
            bool occluded;
        };
        std::vector<PathVisSegment> pathing_vis_segments;
        std::mutex pathing_vis_mutex;
        static void IPLCALL _pathing_vis_callback(IPLVector3 from, IPLVector3 to, IPLbool occluded, void* userData);
        static void IPLCALL _custom_batched_closest_hit(IPLint32 numRays, const IPLRay* rays,
            const IPLfloat32* minDistances, const IPLfloat32* maxDistances, IPLHit* hits, void* userData);
        static void IPLCALL _custom_batched_any_hit(IPLint32 numRays, const IPLRay* rays,
            const IPLfloat32* minDistances, const IPLfloat32* maxDistances, IPLuint8* occluded, void* userData);

        RayTraceDebugContext ray_trace_debug_context_;
        std::atomic<int> ray_debug_bounce_index_{0};

        // Parametric reverb cache: when audio thread can't get simulation_mutex, use last-known-good values
        struct CachedParametricReverb {
            float reverbTimes[3] = {0};
            float eq[3] = {0};
            bool valid = false;
        };
        std::unordered_map<int32_t, CachedParametricReverb> reverb_param_cache_;
        std::mutex reverb_cache_mutex_;

        // Threading
        std::mutex simulation_mutex;
        std::thread worker_thread;
        std::mutex worker_mutex;
        std::condition_variable worker_cv;
        std::atomic<bool> thread_running = false;
        std::atomic<bool> simulation_requested = false;
        std::atomic<bool> scene_dirty = false;
        std::atomic<uint32_t> geometry_update_throttle_counter{0};
        int geometry_update_throttle = 4;  // Apply scene commit every Nth transform-only update (from ResonanceRuntimeConfig)
        int simulation_tick_throttle = 1;  // Run simulation every Nth tick (1=every frame, 2=every 2nd)
        std::atomic<uint32_t> tick_throttle_counter{0};
        // Simulation update interval: Direct runs every tick; Reflections+Pathing only when interval elapsed
        float simulation_update_interval = 0.1f;  // Seconds (0.1 = 100ms default)
        float simulation_update_time_elapsed = 0.0f;
        std::atomic<bool> reflections_pathing_requested{false};

        // Listener
        std::mutex listener_mutex;
        IPLCoordinateSpace3 pending_listener_coords{};
        bool pending_listener_updated = false;
        std::atomic<bool> pending_listener_valid{true};
        /// True when iplSimulatorRunPathing ran this simulation tick (avoids using stale pathing when skipped)
        std::atomic<bool> pathing_ran_this_tick{false};
        /// Ticks to skip RunPathing after SEH crash (reduces exception storm)
        std::atomic<int> pathing_crash_cooldown{0};
        /// One-time warning when pathing enabled but no pathing data (avoids log spam)
        bool pathing_no_data_warned = false;

        /// When set, bake uses this asset instead of live geometry. Set by editor before bake.
        Ref<ResonanceGeometryAsset> _bake_static_scene_asset;

        // Bake param overrides from ResonanceRuntimeConfig (DRY; replaces ProjectSettings)
        int _bake_num_rays = -1;
        int _bake_num_bounces = -1;
        int _bake_reflection_type = -1;
        float _bake_pathing_vis_range = -1.0f;
        float _bake_pathing_path_range = -1.0f;
        int _bake_pathing_num_samples = -1;
        float _bake_pathing_radius = -1.0f;
        float _bake_pathing_threshold = -1.0f;
        int _bake_num_threads = -1;
        bool _bake_pipeline_pathing = false;

        ResonanceProbeBatchRegistry probe_batch_registry_;
        ResonanceSceneManager scene_manager_;
        static uint64_t _hash_probe_data(const PackedByteArray& pba);
        static bool is_shutting_down_flag;

        // Internal Methods
        void _apply_config(Dictionary config);
        void _worker_thread_func();
        void _init_internal();
        void _init_context_and_devices();
        void _init_scene_and_simulator();
        void _start_worker_thread();
        void _shutdown_steam_audio();
        void _update_source_internal(IPLSource source, int32_t handle, Vector3 position, float radius,
            Vector3 source_forward, Vector3 source_up,
            float directivity_weight, float directivity_power, bool air_absorption_enabled,
            bool use_sim_distance_attenuation, float min_distance,
            bool path_validation_enabled, bool find_alternate_paths,
            int occlusion_samples, int num_transmission_rays,
            int baked_data_variation, Vector3 baked_endpoint_center, float baked_endpoint_radius,
            int32_t pathing_probe_batch_handle = -1);

        int _get_bake_num_rays() const;
        int _get_bake_num_bounces() const;
        int _get_bake_num_threads() const;
        int _get_bake_reflection_type() const;
        float _get_bake_pathing_param(const char* key, float default_val) const;
        int _get_bake_pathing_num_samples() const;
        /// Returns scene for bake. When using asset, populates out_temp_scene and out_temp_mesh - caller must release after bake.
        IPLScene _prepare_bake_scene(IPLScene* out_temp_scene, IPLStaticMesh* out_temp_mesh);
        /// Runs bake_fn with prepared bake scene; handles lock, scene commit, and temp scene/mesh cleanup.
        bool _with_bake_scene(std::function<bool(IPLScene bake_scene)> bake_fn);
        /// Returns true when throttled logic should run (counter incremented). When throttle<=1 always returns true.
        bool _should_run_throttled(std::atomic<uint32_t>& counter, int throttle);
        /// Returns first probe batch that has pathing data, or nullptr. Caller must iplProbeBatchRelease.
        IPLProbeBatch _get_first_batch_with_pathing();
        /// Returns probe batch for pathing: preferred_handle if valid and has pathing, else first with pathing. Caller must iplProbeBatchRelease.
        IPLProbeBatch _get_pathing_batch_for_source(int32_t preferred_handle);
        bool _is_batch_compatible_with_config(int32_t handle) const;

        /// True when reflection type uses IR (Convolution, Hybrid, or TAN)
        bool _uses_convolution_or_hybrid_or_tan() const;
        /// True when reflection type uses parametric reverb (Parametric or Hybrid)
        bool _uses_parametric_or_hybrid() const;
        /// True when baked_type is compatible with current reflection_type
        bool _is_reflection_type_compatible(int baked_type) const;

        IPLContext _ctx() const { return steam_audio_context_ ? steam_audio_context_->get_context() : nullptr; }
        IPLEmbreeDevice _embree() const { return steam_audio_context_ ? steam_audio_context_->get_embree_device() : nullptr; }
        IPLOpenCLDevice _opencl() const { return steam_audio_context_ ? steam_audio_context_->get_opencl_device() : nullptr; }
        IPLRadeonRaysDevice _radeon() const { return steam_audio_context_ ? steam_audio_context_->get_radeon_rays_device() : nullptr; }
        IPLTrueAudioNextDevice _tan() const { return steam_audio_context_ ? steam_audio_context_->get_tan_device() : nullptr; }
        IPLHRTF _hrtf() const { return steam_audio_context_ ? steam_audio_context_->get_hrtf() : nullptr; }
        IPLSceneType _scene_type() const { return steam_audio_context_ ? steam_audio_context_->get_scene_type() : IPL_SCENETYPE_EMBREE; }

    protected:
        static void _bind_methods();

    public:
        ResonanceServer();
        ~ResonanceServer();

        static ResonanceServer* get_singleton();
        /// Called from module uninitialize; ensures clean teardown order before destructor.
        void shutdown();

        // API for GDScript
        void init_audio_engine(Dictionary config);
        /// Re-initialize with new config (e.g. when ResonanceRuntimeConfig overrides toolbar init). Shuts down first.
        void reinit_audio_engine(Dictionary config);

        // Getters
        IPLContext get_context_handle() const { return steam_audio_context_ ? steam_audio_context_->get_context() : nullptr; }
        IPLScene get_scene_handle() const { return scene; }
        IPLSimulator get_simulator_handle() const { return simulator; }
        IPLEmbreeDevice get_embree_device_handle() const { return steam_audio_context_ ? steam_audio_context_->get_embree_device() : nullptr; }
        IPLOpenCLDevice get_opencl_device_handle() const { return steam_audio_context_ ? steam_audio_context_->get_opencl_device() : nullptr; }
        IPLRadeonRaysDevice get_radeon_rays_device_handle() const { return steam_audio_context_ ? steam_audio_context_->get_radeon_rays_device() : nullptr; }
        IPLHRTF get_hrtf_handle() const { return steam_audio_context_ ? steam_audio_context_->get_hrtf() : nullptr; }
        IPLReflectionMixer get_reflection_mixer_handle() const { return reflection_mixer; }
        IPLCoordinateSpace3 get_current_listener_coords();
        IPLSceneType get_scene_type() const { return steam_audio_context_ ? steam_audio_context_->get_scene_type() : IPL_SCENETYPE_EMBREE; }

        // Thread Safety
        void lock_mixer() { mixer_access_mutex.lock(); }
        void unlock_mixer() { mixer_access_mutex.unlock(); }
        /// RAII guard for mixer mutex; use instead of manual lock/unlock to ensure unlock on exception
        std::unique_lock<std::mutex> scoped_mixer_lock() { return std::unique_lock<std::mutex>(mixer_access_mutex); }
        void lock_simulation() { simulation_mutex.lock(); }
        void unlock_simulation() { simulation_mutex.unlock(); }

        // Status
        String get_version();
        bool is_initialized() const;
        bool is_simulating() const;
        int get_sample_rate() const { return current_sample_rate; }
        int get_audio_frame_size() const { return frame_size; }
        int get_ambisonic_order() const { return ambisonic_order; }
        /// Reverb Bus debugging: mixer feeds, effect process stats, output levels.
        Dictionary get_reverb_bus_instrumentation() const;
        /// Called by ResonancePlayer when it feeds the reflection mixer (Convolution only).
        void record_mixer_feed() { reverb_mixer_feed_count.fetch_add(1, std::memory_order_relaxed); }
        /// Call when feeding mixer for convolution; ir_non_null, reverb_gain, input_rms (mono before gain)
        void record_convolution_feed(bool ir_non_null, float reverb_gain, float input_rms);
        /// Called by ResonanceAudioEffectInstance each _process.
        void update_reverb_effect_instrumentation(bool mixer_null, bool success, int32_t frames_written, float output_peak);
        float get_max_reverb_duration() const { return max_reverb_duration; }
        int get_num_channels_for_order() const { return (ambisonic_order + 1) * (ambisonic_order + 1); }
        static bool is_shutting_down() { return is_shutting_down_flag; }

        // IO / Baking
        /// Set bake params from ResonanceRuntimeConfig.get_bake_params(). Call before baking when not using ProjectSettings.
        void set_bake_params(Dictionary params);
        /// Set static scene asset for bake (from ResonanceStaticScene). When set, bake uses this instead of live geometry.
        void set_bake_static_scene_asset(const Ref<ResonanceGeometryAsset>& p_asset);
        /// Load static scene from asset into the server's scene (runtime). Replaces any existing static meshes.
        void load_static_scene_from_asset(const Ref<ResonanceGeometryAsset>& p_asset);
        /// Add static scene from asset (additive loading). Use with clear_static_scenes + add for each scene.
        void add_static_scene_from_asset(const Ref<ResonanceGeometryAsset>& p_asset);
        /// Clear all loaded static scene meshes (e.g. before additive load).
        void clear_static_scenes();
        /// Hint for bake log: pathing will run after reflections in this bake pipeline. Call before bake_probes.
        void set_bake_pipeline_pathing(bool p_pathing);
        void save_scene_data(String filename);
        void load_scene_data(String filename);
        /// Export static ResonanceGeometry (dynamic=false) under scene_root to merged asset. Returns OK on success. Standalone; no server required.
        Error export_static_scene_to_asset(Node* scene_root, const String& p_path);
        /// Hash of static geometry for change detection. Same input as export; use to skip re-export when unchanged.
        int64_t get_static_scene_hash(Node* scene_root);
        /// Export scene geometry to OBJ+MTL for debug (iplSceneSaveOBJ). Pass path without extension, e.g. "res://debug/scene".
        void save_scene_obj(String file_base_name);
        PackedVector3Array generate_manual_grid(const Transform3D& volume_transform, Vector3 extents, float spacing,
            int generation_type = 2, float height_above_floor = 1.5f);
        /// Scene-aware probe placement (Steam Audio iplProbeArrayGenerateProbes). For GEN_UNIFORM_FLOOR/GEN_CENTROID.
        /// Returns empty if no scene/0 probes; caller should fall back to generate_manual_grid.
        PackedVector3Array generate_probes_scene_aware(const Transform3D& volume_transform, Vector3 extents, float spacing,
            int generation_type, float height_above_floor);
        bool bake_manual_grid(const PackedVector3Array& points, Ref<ResonanceProbeData> probe_data_res);
        /// Bake reflections. Uses Steam Probe Array API for Centroid/UniformFloor (scene-aware); manual grid for Volume.
        bool bake_probes_for_volume(const Transform3D& volume_transform, Vector3 extents, float spacing,
            int generation_type, float height_above_floor, Ref<ResonanceProbeData> probe_data_res);
        bool bake_pathing(Ref<ResonanceProbeData> probe_data_res);
        bool bake_static_source(Ref<ResonanceProbeData> probe_data_res, Vector3 endpoint_position, float influence_radius);
        bool bake_static_listener(Ref<ResonanceProbeData> probe_data_res, Vector3 endpoint_position, float influence_radius);
        void emit_bake_progress(float progress);
        /// Cancel a reflections bake in progress. Call from another thread (e.g. main) while bake runs in a worker thread.
        void cancel_reflections_bake();
        /// Cancel a pathing bake in progress. Call from another thread (e.g. main) while bake runs in a worker thread.
        void cancel_pathing_bake();
        /// Loads probe data and adds to simulator. Returns handle >= 0 on success, -1 on failure. Call remove_probe_batch when volume exits.
        int32_t load_probe_batch(Ref<ResonanceProbeData> probe_data_res);
        /// Removes a specific probe batch by handle (call from ResonanceProbeVolume._exit_tree).
        void remove_probe_batch(int32_t handle);
        /// Clears all loaded probe batches (e.g. on plugin disable).
        void clear_probe_batches();
        /// Removes probe batches incompatible with current reflection_type/pathing_enabled. Returns count removed.
        int revalidate_probe_batches_with_config();

        // Runtime Control
        void set_debug_occlusion(bool p_enabled);
        bool is_debug_occlusion_enabled() const;
        void set_debug_reflections(bool p_enabled);
        bool is_debug_reflections_enabled() const;
        void set_debug_pathing(bool p_enabled);
        bool is_debug_pathing_enabled() const;
        /// Returns array of {from: Vector3, to: Vector3, occluded: bool} for path visualization (only populated when debug_pathing enabled)
        Array get_pathing_visualization_segments();
        /// Returns array of {from: Vector3, to: Vector3, bounce: int} for reflection ray debug (only when debug_reflections + realtime_rays)
        Array get_ray_debug_segments();
        /// Same as get_ray_debug_segments but uses the given origin instead of pending_listener_coords. Use for viz to avoid 1-frame delay.
        Array get_ray_debug_segments_at(Vector3 origin);
        bool uses_custom_ray_tracer() const;
        bool wants_debug_reflection_viz() const { return debug_reflections && max_rays > 0; }
        int register_debug_mesh(const std::vector<IPLVector3>& vertices, const std::vector<IPLTriangle>& triangles,
            const IPLint32* material_indices, const IPLMatrix4x4* transform, const IPLMaterial* material);
        void unregister_debug_mesh(int mesh_id);
        void set_output_direct_enabled(bool p_enabled);
        bool is_output_direct_enabled() const;
        void set_output_reverb_enabled(bool p_enabled);
        bool is_output_reverb_enabled() const;
        void set_reverb_influence_radius(float p_radius);
        float get_reverb_influence_radius() const;
        void set_reverb_max_distance(float p_dist);
        float get_reverb_max_distance() const;
        void set_reverb_transmission_amount(float p_amount);
        float get_reverb_transmission_amount() const;
        void set_perspective_correction_enabled(bool p_enabled);
        bool is_perspective_correction_enabled() const;
        void set_perspective_correction_factor(float p_factor);
        float get_perspective_correction_factor() const;
        int get_reflection_type() const { return reflection_type; }
        int get_realtime_rays() const { return max_rays; }
        int get_transmission_type() const { return transmission_type; }
        int get_occlusion_type() const { return occlusion_type; }
        bool use_reverb_binaural() const;
        bool use_virtual_surround_output() const { return use_virtual_surround; }
        bool get_hrtf_interpolation_bilinear() const { return hrtf_interpolation_bilinear; }
        bool is_pathing_enabled() const { return pathing_enabled; }
        /// Set custom pathing deviation model (C++ only). For default/UTD pass nullptr. Call clear_pathing_deviation_callback() to reset.
        void set_pathing_deviation_callback(IPLDeviationCallback callback, void* userData);
        void clear_pathing_deviation_callback();
        void set_pathing_enabled(bool p_enabled) { pathing_enabled = p_enabled; }
        /// True when RunPathing ran this tick; false when it crashed/skipped.
        bool did_pathing_run_this_tick() const { return pathing_ran_this_tick.load(); }

        // Calculations
        float calculate_distance_attenuation(Vector3 source_pos, Vector3 listener_pos, float min_dist, float max_dist);
        Vector3 calculate_air_absorption(Vector3 source_pos, Vector3 listener_pos);
        float calculate_directivity(Vector3 source_pos, Vector3 source_forward, Vector3 source_up, Vector3 source_right, Vector3 listener_pos, float weight, float power);

        // Updates
        void notify_geometry_changed(int triangle_delta);
        void update_listener(Vector3 pos, Vector3 dir, Vector3 up);
        void set_listener_valid(bool valid);
        void tick(float delta);
        void update_source(int32_t handle, Vector3 position, float radius,
            Vector3 source_forward = Vector3(0, 0, -1),
            Vector3 source_up = Vector3(0, 1, 0),
            float directivity_weight = 0.0f,
            float directivity_power = 1.0f,
            bool air_absorption_enabled = true,
            bool use_sim_distance_attenuation = false,
            float min_distance = 1.0f,
            bool path_validation_enabled = true,
            bool find_alternate_paths = true,
            int occlusion_samples = 64,
            int num_transmission_rays = 32,
            int baked_data_variation = 0,
            Vector3 baked_endpoint_center = Vector3(0, 0, 0),
            float baked_endpoint_radius = 0.0f,
            int32_t pathing_probe_batch_handle = -1);
        /// Set attenuation callback data for Linear/Curve modes. Call before update_source when attenuation_mode is 1 or 2.
        void set_source_attenuation_callback_data(int32_t handle, int attenuation_mode, float min_distance, float max_distance, const PackedFloat32Array& curve_samples);

        // Handles
        int32_t create_source_handle(Vector3 position, float radius);
        void destroy_source_handle(int32_t handle);
        IPLSource get_source_from_handle(int32_t handle);

        // Data Fetch
        OcclusionData get_source_occlusion_data(int32_t handle);
        bool fetch_reverb_params(int32_t handle, IPLReflectionEffectParams& out_params);
        bool fetch_pathing_params(int32_t handle, IPLPathEffectParams& out_params);
    };

} // namespace godot

#endif
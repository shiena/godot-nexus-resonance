#include "resonance_constants.h"
#include "resonance_geometry.h"
#include "resonance_log.h"
#include "resonance_math.h"
#include "resonance_server.h"
#include "resonance_utils.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/audio_server.hpp>
#if defined(_WIN32) && defined(_MSC_VER)
#include <excpt.h>
#endif
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <limits>

using namespace godot;

namespace {
#if defined(_WIN32) && defined(_MSC_VER)
// SEH must not run in the same function as C++ objects with destructors (e.g. lock_guard) when using /EHsc.
void run_pathing_seh(IPLSimulator sim, int* out_ok) {
    *out_ok = 0;
    __try {
        iplSimulatorRunPathing(sim);
        *out_ok = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out_ok = 0;
    }
}
#endif
String ambient_order_ordinal(int64_t n) {
    uint64_t abs_part;
    if (n == std::numeric_limits<int64_t>::min())
        abs_part = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1u;
    else if (n < 0)
        abs_part = static_cast<uint64_t>(-n);
    else
        abs_part = static_cast<uint64_t>(n);
    int64_t mod10 = static_cast<int64_t>(abs_part % 10ULL);
    int64_t mod100 = static_cast<int64_t>(abs_part % 100ULL);
    if (mod100 >= 11 && mod100 <= 13)
        return String::num_int64(n) + "th";
    if (mod10 == 1)
        return String::num_int64(n) + "st";
    if (mod10 == 2)
        return String::num_int64(n) + "nd";
    if (mod10 == 3)
        return String::num_int64(n) + "rd";
    return String::num_int64(n) + "th";
}
} // namespace

std::atomic<bool> ResonanceServer::is_shutting_down_flag{false};
static ResonanceServer* g_resonance_server_singleton = nullptr;

ResonanceServer::ResonanceServer() {
    g_resonance_server_singleton = this;
    // No auto-init here!
}

ResonanceServer::~ResonanceServer() {
    is_shutting_down_flag.store(true, std::memory_order_release);
    _shutdown_steam_audio();
    if (g_resonance_server_singleton == this)
        g_resonance_server_singleton = nullptr;
}

ResonanceServer* ResonanceServer::get_singleton() { return g_resonance_server_singleton; }

void ResonanceServer::register_ipl_context_client(void* key, IplContextClientCleanup cleanup) {
    if (!key || !cleanup)
        return;
    std::lock_guard<std::mutex> lock(ipl_context_clients_mutex_);
    for (IplContextClient& e : ipl_context_clients_) {
        if (e.key == key) {
            e.cleanup = cleanup;
            return;
        }
    }
    ipl_context_clients_.push_back(IplContextClient{key, cleanup});
}

void ResonanceServer::unregister_ipl_context_client(void* key) {
    if (!key)
        return;
    std::lock_guard<std::mutex> lock(ipl_context_clients_mutex_);
    auto& v = ipl_context_clients_;
    v.erase(std::remove_if(v.begin(), v.end(), [key](const IplContextClient& e) { return e.key == key; }), v.end());
}

void ResonanceServer::_drain_ipl_context_clients_before_context_destroy() {
    std::vector<IplContextClient> clients_copy;
    {
        std::lock_guard<std::mutex> lock(ipl_context_clients_mutex_);
        clients_copy = std::move(ipl_context_clients_);
        ipl_context_clients_.clear();
    }
    AudioServer* audio = AudioServer::get_singleton();
    if (audio)
        audio->lock();
    for (const IplContextClient& c : clients_copy) {
        if (c.cleanup && c.key)
            c.cleanup(c.key);
    }
    if (audio)
        audio->unlock();
}

void ResonanceServer::shutdown() {
    is_shutting_down_flag.store(true, std::memory_order_release);
    _shutdown_steam_audio();
}

// --- PUBLIC CONFIG & INIT ---

void ResonanceServer::_apply_config(Dictionary config) {
    config_.apply(config, [this](const char* key, float def) { return _get_bake_pathing_param(key, def); });
    if (config.has("audio_frame_size_was_auto")) {
        Variant v = config["audio_frame_size_was_auto"];
        audio_frame_size_was_auto_.store(v.operator bool(), std::memory_order_release);
    } else {
        audio_frame_size_was_auto_.store(true, std::memory_order_release);
    }
    current_sample_rate = config_.sample_rate;
    frame_size = config_.frame_size;
    ambisonic_order = config_.ambisonic_order;
    max_reverb_duration = config_.max_reverb_duration;
    simulation_threads = config_.simulation_threads;
    simulation_cpu_cores_percent = config_.simulation_cpu_cores_percent;
    max_rays = config_.max_rays;
    max_bounces = config_.max_bounces;
    reverb_influence_radius = config_.reverb_influence_radius;
    reverb_max_distance = config_.reverb_max_distance;
    reverb_transmission_amount = config_.reverb_transmission_amount;
    reflection_type = config_.reflection_type;
    default_reflections_mode = config_.default_reflections_mode;
    hybrid_reverb_transition_time = config_.hybrid_reverb_transition_time;
    hybrid_reverb_overlap_percent = config_.hybrid_reverb_overlap_percent;
    transmission_type = config_.transmission_type;
    occlusion_type = config_.occlusion_type;
    hrtf_sofa_asset = config_.hrtf_sofa_asset;
    reverb_binaural = config_.reverb_binaural;
    use_virtual_surround = config_.use_virtual_surround;
    hrtf_interpolation_bilinear = config_.hrtf_interpolation_bilinear;
    pathing_enabled = config_.pathing_enabled;
    pathing_vis_radius = config_.pathing_vis_radius;
    pathing_vis_threshold = config_.pathing_vis_threshold;
    pathing_vis_range = config_.pathing_vis_range;
    pathing_normalize_eq = config_.pathing_normalize_eq;
    scene_type = config_.scene_type;
    opencl_device_type = config_.opencl_device_type;
    opencl_device_index = config_.opencl_device_index;
    context_validation = config_.context_validation;
    context_simd_level = config_.context_simd_level;
    realtime_irradiance_min_distance = config_.realtime_irradiance_min_distance;
    realtime_simulation_duration = config_.realtime_simulation_duration;
    realtime_num_diffuse_samples = config_.realtime_num_diffuse_samples;
    output_direct_enabled.store(config_.output_direct_enabled, std::memory_order_relaxed);
    output_reverb_enabled.store(config_.output_reverb_enabled, std::memory_order_relaxed);
    debug_occlusion.store(config_.debug_occlusion, std::memory_order_relaxed);
    debug_reflections.store(config_.debug_reflections, std::memory_order_relaxed);
    debug_pathing.store(config_.debug_pathing, std::memory_order_relaxed);
    perspective_correction_enabled.store(config_.perspective_correction_enabled, std::memory_order_relaxed);
    perspective_correction_factor.store(config_.perspective_correction_factor, std::memory_order_relaxed);
    geometry_update_throttle = config_.geometry_update_throttle;
    simulation_tick_throttle = config_.simulation_tick_throttle;
    simulation_update_interval = config_.simulation_update_interval;
    tick_throttle_counter.store(0, std::memory_order_relaxed);
    simulation_update_time_elapsed = 0.0f;
    reflections_pathing_requested.store(false, std::memory_order_relaxed);
}

void ResonanceServer::init_audio_engine(Dictionary config) {
    if (_ctx() != nullptr) {
        UtilityFunctions::push_warning("Nexus Resonance: Already initialized.");
        return;
    }
    _apply_config(config);
    _init_internal();
}

void ResonanceServer::reinit_audio_engine(Dictionary config) {
    if (_ctx() == nullptr) {
        init_audio_engine(config);
        return;
    }
    _shutdown_steam_audio();
    _apply_config(config);
    _init_internal();
}

void ResonanceServer::_init_internal() {
    reverb_effect_process_calls.store(0, std::memory_order_relaxed);
    reverb_effect_mixer_null.store(0, std::memory_order_relaxed);
    reverb_effect_success.store(0, std::memory_order_relaxed);
    reverb_effect_frames_written.store(0, std::memory_order_relaxed);
    reverb_effect_output_peak.store(0.0f, std::memory_order_relaxed);
    reverb_mixer_feed_count.store(0, std::memory_order_relaxed);
    reverb_convolution_valid_fetches.store(0, std::memory_order_relaxed);
    reverb_convolution_feed_ir_null.store(0, std::memory_order_relaxed);
    reverb_convolution_gain_min.store(1.0f, std::memory_order_relaxed);
    reverb_convolution_gain_max.store(0.0f, std::memory_order_relaxed);
    reverb_convolution_input_rms_max.store(0.0f, std::memory_order_relaxed);
    instrumentation_fetch_lock_ok.store(0, std::memory_order_relaxed);
    instrumentation_fetch_cache_hit.store(0, std::memory_order_relaxed);
    instrumentation_fetch_cache_miss.store(0, std::memory_order_relaxed);

    _init_context_and_devices();
    if (!steam_audio_context_)
        return;
    if (!_init_scene_and_simulator())
        return;
    _start_worker_thread();

    String version_str = String::num_int64(STEAMAUDIO_VERSION_MAJOR) + "." + String::num_int64(STEAMAUDIO_VERSION_MINOR) + "." + String::num_int64(STEAMAUDIO_VERSION_PATCH);
    const char* refl_names[] = {"Convolution", "Parametric", "Hybrid", "TrueAudio Next"};
    int refl_idx = (reflection_type >= resonance::kReflectionConvolution && reflection_type <= resonance::kReflectionTan) ? reflection_type : resonance::kReflectionConvolution;
    String rays_str = (max_rays == 0) ? "Rays: Baked Only (0)" : "Rays (Realtime): " + String::num_int64(max_rays);
    String engine_msg = "Engine Started (Steam Audio " + version_str + "). Rate: " + String::num_int64(current_sample_rate) +
                        " | Ambient Order: " + ambient_order_ordinal(ambisonic_order) + " | Reflection: " + refl_names[refl_idx] + " | " + rays_str;
    UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] " + engine_msg);
}

void ResonanceServer::_init_context_and_devices() {
    steam_audio_context_ = std::make_unique<ResonanceSteamAudioContext>();
    ResonanceSteamAudioContextConfig ctx_config{};
    ctx_config.sample_rate = current_sample_rate;
    ctx_config.frame_size = frame_size;
    ctx_config.ambisonic_order = ambisonic_order;
    ctx_config.max_reverb_duration = max_reverb_duration;
    ctx_config.reflection_type = reflection_type;
    ctx_config.scene_type = scene_type;
    ctx_config.opencl_device_type = opencl_device_type;
    ctx_config.opencl_device_index = opencl_device_index;
    ctx_config.context_validation = context_validation;
    ctx_config.context_simd_level = context_simd_level;
    ctx_config.hrtf_sofa_asset = hrtf_sofa_asset;

    if (!steam_audio_context_->init(ctx_config)) {
        steam_audio_context_.reset();
        return;
    }
    reflection_type = ctx_config.reflection_type;
    scene_type = ctx_config.scene_type;

    if (debug_reflections.load(std::memory_order_acquire) && max_rays > 0) {
        ray_trace_debug_context_.clear();
        UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] Debug Reflections enabled – using Embree + standalone ray viz (no CUSTOM scene).");
    }
}

bool ResonanceServer::_init_scene_and_simulator() {
    IPLAudioSettings audioSettings{current_sample_rate, frame_size};
    IPLSceneSettings sceneSettings{};
    sceneSettings.type = _scene_type();
    sceneSettings.embreeDevice = _embree();
    sceneSettings.radeonRaysDevice = _radeon();
    if (iplSceneCreate(_ctx(), &sceneSettings, &scene) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceServer: iplSceneCreate failed.");
        steam_audio_context_.reset();
        return false;
    }

    simulation_settings.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    simulation_settings.sceneType = _scene_type();
    simulation_settings.reflectionType =
        (reflection_type == resonance::kReflectionParametric) ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC : (reflection_type == resonance::kReflectionHybrid) ? IPL_REFLECTIONEFFECTTYPE_HYBRID
                                                                                                  : (reflection_type == resonance::kReflectionTan)      ? IPL_REFLECTIONEFFECTTYPE_TAN
                                                                                                                                                        : IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
    simulation_settings.openCLDevice = _opencl();
    simulation_settings.tanDevice = _tan();
    simulation_settings.maxNumOcclusionSamples = resonance::kMaxOcclusionSamples;
    simulation_settings.maxNumRays = _uses_convolution_or_hybrid_or_tan() ? (max_rays > 0 ? max_rays : 1) : max_rays;
    simulation_settings.numDiffuseSamples = realtime_num_diffuse_samples;
    simulation_settings.maxDuration = max_reverb_duration;
    simulation_settings.samplingRate = current_sample_rate;
    simulation_settings.frameSize = frame_size;
    simulation_settings.maxOrder = ambisonic_order;
    simulation_settings.numThreads = simulation_threads;
    simulation_settings.maxNumSources = resonance::kMaxSimulationSources;
    simulation_settings.numVisSamples = pathing_enabled ? resonance::kBakePathingDefaultNumSamples : 1;
    simulation_settings.rayBatchSize = 1;

    if (iplSimulatorCreate(_ctx(), &simulation_settings, &simulator) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceServer: iplSimulatorCreate failed.");
        iplSceneRelease(&scene);
        steam_audio_context_.reset();
        return false;
    }

    // FMOD Bridge: Create reverb source at listener position for iplFMODSetReverbSource.
    fmod_reverb_source_handle_ = create_source_handle(Vector3(0, 0, 0), 1.0f);

    if (reflection_type == resonance::kReflectionConvolution || reflection_type == resonance::kReflectionTan) {
        IPLReflectionEffectSettings rs{};
        rs.type = (reflection_type == resonance::kReflectionTan) ? IPL_REFLECTIONEFFECTTYPE_TAN : IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
        rs.numChannels = get_num_channels_for_order();
        rs.irSize = static_cast<IPLint32>(
            std::lroundf(resonance::sanitize_audio_float(max_reverb_duration) * static_cast<float>(current_sample_rate)));
        IPLReflectionMixer tmp_mixer = nullptr;
        if (iplReflectionMixerCreate(_ctx(), &audioSettings, &rs, &tmp_mixer) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceServer: iplReflectionMixerCreate failed.");
            iplSimulatorRelease(&simulator);
            iplSceneRelease(&scene);
            steam_audio_context_.reset();
            return false;
        }
        {
            std::lock_guard<std::mutex> m_lock(mixer_access_mutex);
            reflection_mixer_[1] = tmp_mixer;
        }
        new_reflection_mixer_written_.store(true, std::memory_order_release);
    }

    iplSceneCommit(scene);
    iplSimulatorSetScene(simulator, scene);
    iplSimulatorCommit(simulator);

    update_listener(Vector3(0, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));
    return true;
}

void ResonanceServer::_start_worker_thread() {
    thread_running = true;
    worker_thread = std::thread(&ResonanceServer::_worker_thread_func, this);
}

// --- RUNTIME LOOP ---

void ResonanceServer::tick(float delta) {
    // Accumulate time; Reflections+Pathing only when interval elapsed (reduces CPU)
    simulation_update_time_elapsed += delta;
    if (simulation_update_time_elapsed >= simulation_update_interval) {
        simulation_update_time_elapsed = 0.0f;
        reflections_pathing_requested.store(true, std::memory_order_release);
    }

    if (!_should_run_throttled(tick_throttle_counter, simulation_tick_throttle))
        return;
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        simulation_requested = true;
    }
    worker_cv.notify_one();
}

void ResonanceServer::_worker_thread_func() {
    while (thread_running) {
        std::unique_lock<std::mutex> lock(worker_mutex);
        worker_cv.wait(lock, [this] { return simulation_requested || !thread_running; });

        if (!thread_running)
            break;
        simulation_requested = false;

        IPLCoordinateSpace3 current_listener;
        if (new_listener_written_.exchange(false, std::memory_order_acq_rel)) {
            listener_coords_[0] = listener_coords_[1];
        }
        current_listener = listener_coords_[0];
        lock.unlock();

        if (_ctx() && simulator) {
            // Skip reflections simulation when no data source: saves CPU, avoids Steam Audio fallback.
            bool run_reflections = (max_rays > 0);
            if (!run_reflections) {
                run_reflections = probe_batch_registry_.has_any_batches();
            }

            std::lock_guard<std::mutex> sim_lock(simulation_mutex);

            // Update shared inputs every tick so numRays (realtime rays) and listener stay current.
            // numRays=0: baked-only (probe interpolation); numRays>0: realtime raytracing.
            IPLSimulationSharedInputs inputs{};
            inputs.listener = current_listener;
            inputs.numRays = max_rays;
            inputs.numBounces = max_bounces;
            inputs.duration = realtime_simulation_duration;
            inputs.order = ambisonic_order;
            inputs.irradianceMinDistance = realtime_irradiance_min_distance;
            inputs.pathingVisCallback = (pathing_enabled && debug_pathing.load(std::memory_order_acquire)) ? _pathing_vis_callback : nullptr;
            inputs.pathingUserData = (pathing_enabled && debug_pathing.load(std::memory_order_acquire)) ? static_cast<void*>(this) : nullptr;
            IPLSimulationFlags sim_flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | (run_reflections ? IPL_SIMULATIONFLAGS_REFLECTIONS : 0));
            if (pathing_enabled)
                sim_flags = static_cast<IPLSimulationFlags>(sim_flags | IPL_SIMULATIONFLAGS_PATHING);
            iplSimulatorSetSharedInputs(simulator, sim_flags, &inputs);

            if (scene_dirty) {
                iplSceneCommit(scene);
                iplSimulatorSetScene(simulator, scene);
                iplSimulatorCommit(simulator);
                scene_dirty = false;
            }

            iplSimulatorRunDirect(simulator);
            _worker_note_direct_sim_pass_completed();
            // Reflections and Pathing only when simulation_update_interval elapsed (saves CPU)
            pathing_ran_this_tick.store(false);
            bool run_heavy = reflections_pathing_requested.exchange(false, std::memory_order_acq_rel);
            if (run_heavy) {
                if (run_reflections) {
                    iplSimulatorRunReflections(simulator);
                    reflections_have_run_once_.store(true);
                    {
                        std::lock_guard<std::mutex> lock(reflections_pending_mutex_);
                        reflections_pending_handles_.clear();
                    }
                }
                // Reduce crash cooldown each tick
                int cooldown = pathing_crash_cooldown.load();
                if (cooldown > 0)
                    pathing_crash_cooldown.store(cooldown - 1);
                if (pathing_enabled && pending_listener_valid.load() && pathing_crash_cooldown.load() <= 0) {
                    if (debug_pathing.load(std::memory_order_acquire)) {
                        std::lock_guard<std::mutex> pv_lock(pathing_vis_mutex);
                        pathing_vis_segments.clear();
                    }
#if defined(_WIN32) && defined(_MSC_VER)
                    int pathing_ok = 0;
                    run_pathing_seh(simulator, &pathing_ok);
                    if (pathing_ok) {
                        pathing_ran_this_tick.store(true);
                    } else {
                        pathing_crash_cooldown.store(resonance::kPathingCrashCooldownTicks);
                    }
#else
                    iplSimulatorRunPathing(simulator);
                    pathing_ran_this_tick.store(true);
#endif
                }
            }
        }
    }
}

// --- SHUTDOWN ---

void ResonanceServer::_shutdown_steam_audio() {
    if (!_ctx())
        return; // Idempotent; safe to call multiple times

    // Reset atomic flags first to prevent late accesses during/after shutdown
    new_listener_written_.store(false);
    pending_listener_valid.store(false);
    simulation_requested.store(false);
    reflections_pathing_requested.store(false);
    scene_dirty.store(false);
    spatial_audio_warmup_passes_remaining_.store(0, std::memory_order_release);
    pathing_ran_this_tick.store(false);
    reflections_have_run_once_.store(false);
    {
        std::lock_guard<std::mutex> lock(reflections_pending_mutex_);
        reflections_pending_handles_.clear();
    }

    if (thread_running) {
        thread_running = false;
        worker_cv.notify_all();
        if (worker_thread.joinable())
            worker_thread.join();
    }
    {
        std::lock_guard<std::recursive_mutex> cb_lock(_attenuation_callback_mutex);
        _source_attenuation_entries.clear();
    }
    _source_update_snapshot_.clear();
    realtime_reflection_log_once_handles_.clear();
    {
        std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
        reverb_param_cache_read_.clear();
        reverb_param_cache_write_.clear();
        reverb_cache_dirty_.store(false);
    }
    {
        std::lock_guard<std::mutex> c_lock(reflection_cache_mutex_);
        reflection_param_cache_read_.clear();
        reflection_param_cache_write_.clear();
        reflection_cache_dirty_.store(false);
    }
    {
        std::lock_guard<std::mutex> c_lock(pathing_cache_mutex_);
        pathing_param_cache_read_.clear();
        pathing_param_cache_write_.clear();
        pathing_param_output_.clear();
        pathing_cache_dirty_.store(false);
    }
    // Clean probe batches (remove from simulator before releasing). Registry lock order: do this before simulation_mutex.
    std::vector<IPLProbeBatch> batches_to_release;
    probe_batch_registry_.get_all_batches_for_shutdown(batches_to_release);

    // Hold simulation_mutex for all IPL simulator/scene/source teardown so audio-thread try_lock paths cannot interleave.
    {
        std::lock_guard<std::mutex> sim_lock(simulation_mutex);
        for (IPLProbeBatch batch : batches_to_release) {
            if (simulator && batch) {
                iplSimulatorRemoveProbeBatch(simulator, batch);
            }
            if (batch)
                iplProbeBatchRelease(&batch);
        }
        if (simulator && !batches_to_release.empty())
            iplSimulatorCommit(simulator);

        // FMOD Bridge: destroy reverb source before simulator release (cannot use destroy_source_handle: is_shutting_down blocks it).
        if (fmod_reverb_source_handle_ >= 0) {
            _destroy_source_handle_under_simulation_lock(fmod_reverb_source_handle_);
            fmod_reverb_source_handle_ = -1;
        }

        new_reflection_mixer_written_.store(false);
        {
            std::lock_guard<std::mutex> m_lock(mixer_access_mutex);
            if (reflection_mixer_[0]) {
                iplReflectionMixerRelease(&reflection_mixer_[0]);
                reflection_mixer_[0] = nullptr;
            }
            if (reflection_mixer_[1]) {
                iplReflectionMixerRelease(&reflection_mixer_[1]);
                reflection_mixer_[1] = nullptr;
            }
        }
        if (simulator)
            iplSimulatorRelease(&simulator);
        for (IPLStaticMesh m : _runtime_static_meshes) {
            if (m && scene) {
                iplStaticMeshRemove(m, scene);
                iplStaticMeshRelease(&m);
            }
        }
        _runtime_static_meshes.clear();
        for (IPLInstancedMesh& im : _runtime_static_instanced_meshes) {
            if (im && scene) {
                iplInstancedMeshRemove(im, scene);
                iplInstancedMeshRelease(&im);
            }
        }
        _runtime_static_instanced_meshes.clear();
        for (IPLScene& sub : _runtime_static_sub_scenes) {
            if (sub)
                iplSceneRelease(&sub);
        }
        _runtime_static_sub_scenes.clear();
        if (scene)
            iplSceneRelease(&scene);
    }
    if (steam_audio_context_) {
        _drain_ipl_context_clients_before_context_destroy();
        steam_audio_context_->shutdown();
        steam_audio_context_.reset();
    }
}
String ResonanceServer::get_version() { return String("Nexus Resonance v") + resonance::kVersion; }
bool ResonanceServer::is_initialized() const { return (_ctx() != nullptr); }
bool ResonanceServer::is_simulating() const { return is_initialized() && global_triangle_count > 0; }

bool ResonanceServer::is_spatial_audio_output_ready() const {
    if (!is_initialized())
        return true;
    return spatial_audio_warmup_passes_remaining_.load(std::memory_order_acquire) <= 0;
}

void ResonanceServer::reset_spatial_audio_warmup_passes() {
    if (!is_initialized())
        return;
    spatial_audio_warmup_passes_remaining_.store(resonance::kSpatialAudioWarmupWorkerPasses, std::memory_order_release);
}

void ResonanceServer::_worker_note_direct_sim_pass_completed() {
    int v = spatial_audio_warmup_passes_remaining_.load(std::memory_order_relaxed);
    if (v > 0)
        spatial_audio_warmup_passes_remaining_.store(v - 1, std::memory_order_release);
}

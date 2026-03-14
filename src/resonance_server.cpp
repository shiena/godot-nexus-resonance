#include "resonance_server.h"
#include "ray_trace_debug_context.h"
#include "resonance_constants.h"
#include "resonance_debug_agent.h"
#include "resonance_geometry.h"
#include "resonance_geometry_asset.h"
#include "resonance_ipl_guard.h"
#include "resonance_log.h"
#include "resonance_material.h"
#include "resonance_math.h"
#if defined(_WIN32) && defined(_MSC_VER)
#include <excpt.h>
#endif
#include <algorithm>
#include <climits>
#include <cstring>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <set>

using namespace godot;

namespace {
void bake_progress_callback(float p, void* ud) {
    static_cast<godot::ResonanceServer*>(ud)->emit_bake_progress(p);
}
String _ambient_order_ordinal(int64_t n) {
    int64_t mod10 = ((n < 0) ? -n : n) % 10;
    int64_t mod100 = ((n < 0) ? -n : n) % 100;
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

// ----------------------------------------------------------------------------
// SOURCE MANAGER IMPLEMENTATION
// ----------------------------------------------------------------------------

SourceManager::SourceManager() {}
SourceManager::~SourceManager() {
    std::lock_guard<std::mutex> lock(mutex);
    release_all();
}

int32_t SourceManager::add_source(IPLSource source) {
    if (!source)
        return -1;
    IPLSource retained_source = iplSourceRetain(source);
    std::lock_guard<std::mutex> lock(mutex);
    int32_t handle = alloc_handle();
    if (handle < 0) {
        iplSourceRelease(&retained_source);
        ResonanceLog::error("SourceManager: Handle overflow (max sources exceeded).");
        return -1;
    }
    items[handle] = retained_source;
    return handle;
}

void SourceManager::remove_source(int32_t handle) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = items.find(handle);
    if (it != items.end()) {
        _handle_release_source(&(it->second));
        items.erase(it);
        recycle_handle(handle);
    }
}

IPLSource SourceManager::get_source(int32_t handle) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = items.find(handle);
    if (it != items.end()) {
        return iplSourceRetain(it->second);
    }
    return nullptr;
}

void SourceManager::get_all_handles(std::vector<int32_t>& out) {
    std::lock_guard<std::mutex> lock(mutex);
    out.clear();
    for (const auto& pair : items) {
        out.push_back(pair.first);
    }
}

// ----------------------------------------------------------------------------
// PROBE BATCH MANAGER IMPLEMENTATION
// ----------------------------------------------------------------------------

ProbeBatchManager::ProbeBatchManager() {}
ProbeBatchManager::~ProbeBatchManager() {
    std::lock_guard<std::mutex> lock(mutex);
    release_all();
}

int32_t ProbeBatchManager::add_batch(IPLProbeBatch batch) {
    if (!batch)
        return -1;
    std::lock_guard<std::mutex> lock(mutex);
    int32_t handle = alloc_handle();
    if (handle < 0) {
        iplProbeBatchRelease(&batch);
        ResonanceLog::error("ProbeBatchManager: Handle overflow (max probe batches exceeded).");
        return -1;
    }
    items[handle] = batch;
    return handle;
}

IPLProbeBatch ProbeBatchManager::take_batch(int32_t handle) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = items.find(handle);
    if (it != items.end()) {
        IPLProbeBatch batch = it->second;
        items.erase(it);
        recycle_handle(handle);
        return batch;
    }
    return nullptr;
}

void ProbeBatchManager::get_all_batches(std::vector<IPLProbeBatch>& out) {
    std::lock_guard<std::mutex> lock(mutex);
    clear_and_reset(out);
}

IPLProbeBatch ProbeBatchManager::get_first_batch() {
    std::lock_guard<std::mutex> lock(mutex);
    if (items.empty())
        return nullptr;
    return iplProbeBatchRetain(items.begin()->second);
}

IPLProbeBatch ProbeBatchManager::get_batch(int32_t handle) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = items.find(handle);
    if (it != items.end())
        return iplProbeBatchRetain(it->second);
    return nullptr;
}

// ----------------------------------------------------------------------------
// RESONANCE SERVER IMPLEMENTATION
// ----------------------------------------------------------------------------

bool ResonanceServer::is_shutting_down_flag = false;
static ResonanceServer* _singleton = nullptr;

void IPLCALL ResonanceServer::_pathing_vis_callback(IPLVector3 from, IPLVector3 to, IPLbool occluded, void* userData) {
    if (ResonanceServer::is_shutting_down_flag)
        return;
    ResonanceServer* server = static_cast<ResonanceServer*>(userData);
    if (!server)
        return;
    PathVisSegment seg;
    seg.from = ResonanceUtils::to_godot_vector3(from);
    seg.to = ResonanceUtils::to_godot_vector3(to);
    seg.occluded = (occluded == IPL_TRUE);
    std::lock_guard<std::mutex> lock(server->pathing_vis_mutex);
    server->pathing_vis_segments.push_back(seg);
}

void IPLCALL ResonanceServer::_custom_batched_closest_hit(IPLint32 numRays, const IPLRay* rays,
                                                          const IPLfloat32* minDistances, const IPLfloat32* maxDistances, IPLHit* hits, void* userData) {
    if (ResonanceServer::is_shutting_down_flag)
        return;
    ResonanceServer* server = static_cast<ResonanceServer*>(userData);
    if (!server)
        return;

    int bounce = server->ray_debug_bounce_index_.load(std::memory_order_relaxed);
    server->ray_trace_debug_context_.trace_batch(numRays, rays, minDistances, maxDistances, hits, bounce);
    server->ray_trace_debug_context_.push_rays_for_viz(numRays, rays, hits, bounce);
    server->ray_debug_bounce_index_.store(bounce + 1, std::memory_order_relaxed);
}

void IPLCALL ResonanceServer::_custom_batched_any_hit(IPLint32 numRays, const IPLRay* rays,
                                                      const IPLfloat32* minDistances, const IPLfloat32* maxDistances, IPLuint8* occluded, void* userData) {
    if (ResonanceServer::is_shutting_down_flag)
        return;
    ResonanceServer* server = static_cast<ResonanceServer*>(userData);
    if (!server)
        return;

    std::vector<IPLHit> hits(numRays);
    int bounce = server->ray_debug_bounce_index_.load(std::memory_order_relaxed);
    server->ray_trace_debug_context_.trace_batch(numRays, rays, minDistances, maxDistances, hits.data(), bounce);
    for (IPLint32 i = 0; i < numRays; i++) {
        occluded[i] = (hits[i].distance < INFINITY) ? 1 : 0;
    }
}

ResonanceServer::ResonanceServer() {
    _singleton = this;
    // No auto-init here!
}

ResonanceServer::~ResonanceServer() {
    is_shutting_down_flag = true;
    _shutdown_steam_audio();
    if (_singleton == this)
        _singleton = nullptr;
}

ResonanceServer* ResonanceServer::get_singleton() { return _singleton; }

void ResonanceServer::shutdown() {
    is_shutting_down_flag = true;
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
    output_direct_enabled = config_.output_direct_enabled;
    output_reverb_enabled = config_.output_reverb_enabled;
    debug_occlusion = config_.debug_occlusion;
    debug_reflections = config_.debug_reflections;
    debug_pathing = config_.debug_pathing;
    perspective_correction_enabled = config_.perspective_correction_enabled;
    perspective_correction_factor = config_.perspective_correction_factor;
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
    String rays_str = (max_rays == 0) ? "Rays (Baked Only)" : "Rays (Realtime): " + String::num_int64(max_rays);
    String engine_msg = "Engine Started (Steam Audio " + version_str + "). Rate: " + String::num_int64(current_sample_rate) +
                        " | Ambient Order: " + _ambient_order_ordinal(ambisonic_order) + " | Reflection: " + refl_names[refl_idx] + " | " + rays_str;
    UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] " + engine_msg);
    Engine* eng = Engine::get_singleton();
    if (max_rays == 0 && _uses_convolution_or_hybrid_or_tan() && (!eng || !eng->is_editor_hint())) {
        UtilityFunctions::push_warning("Nexus Resonance: Rays=0 (Baked Only). Convolution/Hybrid/TAN reverb uses only baked probe data. Bake probes for reverb to work.");
    }
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

    if (debug_reflections && max_rays > 0) {
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
        rs.irSize = (int)(max_reverb_duration * current_sample_rate);
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
            inputs.pathingVisCallback = (pathing_enabled && debug_pathing) ? _pathing_vis_callback : nullptr;
            inputs.pathingUserData = (pathing_enabled && debug_pathing) ? static_cast<void*>(this) : nullptr;
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
                    if (debug_pathing) {
                        std::lock_guard<std::mutex> pv_lock(pathing_vis_mutex);
                        pathing_vis_segments.clear();
                    }
#if defined(_WIN32) && defined(_MSC_VER)
                    __try {
                        iplSimulatorRunPathing(simulator);
                        pathing_ran_this_tick.store(true);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        // Steam Audio RunPathing can crash (SEH) when listener is behind wall / unreachable.
                        // Skip RunPathing for kPathingCrashCooldownTicks to reduce exception storm; avoids hundreds of crashes per second.
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
        std::lock_guard<std::mutex> cb_lock(_attenuation_callback_mutex);
        _source_attenuation_callback_data.clear();
        _source_attenuation_context.clear();
    }
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
    // Clean probe batches (remove from simulator before releasing)
    std::vector<IPLProbeBatch> batches_to_release;
    probe_batch_registry_.get_all_batches_for_shutdown(batches_to_release);
    for (IPLProbeBatch batch : batches_to_release) {
        if (simulator && batch) {
            iplSimulatorRemoveProbeBatch(simulator, batch);
        }
        if (batch)
            iplProbeBatchRelease(&batch);
    }
    if (simulator && !batches_to_release.empty())
        iplSimulatorCommit(simulator);

    // FMOD Bridge: Destroy reverb source before simulator release.
    if (fmod_reverb_source_handle_ >= 0) {
        destroy_source_handle(fmod_reverb_source_handle_);
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
    // Release instanced meshes and sub-scenes (same order as ResonanceSceneManager::clear_static_scenes)
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
    if (steam_audio_context_) {
        steam_audio_context_->shutdown();
        steam_audio_context_.reset();
    }
}

// --- HELPERS / GETTERS ---

String ResonanceServer::get_version() { return String("Nexus Resonance v") + resonance::kVersion; }
bool ResonanceServer::is_initialized() const { return (_ctx() != nullptr); }
bool ResonanceServer::is_simulating() const { return is_initialized() && global_triangle_count > 0; }
IPLCoordinateSpace3 ResonanceServer::get_current_listener_coords() {
    if (new_listener_written_.exchange(false, std::memory_order_acq_rel)) {
        listener_coords_[0] = listener_coords_[1];
    }
    return listener_coords_[0];
}

IPLReflectionMixer ResonanceServer::get_reflection_mixer_handle() const {
    std::lock_guard<std::mutex> lock(mixer_access_mutex);
    if (new_reflection_mixer_written_.exchange(false, std::memory_order_acq_rel)) {
        if (reflection_mixer_[0]) {
            iplReflectionMixerRelease(&reflection_mixer_[0]);
            reflection_mixer_[0] = nullptr;
        }
        if (reflection_mixer_[1]) {
            reflection_mixer_[0] = reflection_mixer_[1];
            reflection_mixer_[1] = nullptr;
        }
    }
    return reflection_mixer_[0];
}

static int snap_to_supported_frame_size(int value) {
    const int supported[] = {256, resonance::kGodotDefaultFrameSize, 1024, resonance::kMaxAudioFrameSize};
    int best = resonance::kGodotDefaultFrameSize;
    int best_dist = INT_MAX;
    for (int s : supported) {
        int d = (value > s) ? (value - s) : (s - value);
        if (d < best_dist) {
            best_dist = d;
            best = s;
        }
    }
    return best;
}

void ResonanceServer::request_reinit_with_frame_size(int detected_frame_count) {
    if (detected_frame_count <= 0)
        return;
    if (!audio_frame_size_was_auto_.load(std::memory_order_acquire))
        return; // User set explicit value; do not override
    int snapped = snap_to_supported_frame_size(detected_frame_count);
    if (snapped == frame_size)
        return; // Already at nearest supported; avoid redundant reinit
    int prev = pending_reinit_frame_size_.exchange(snapped, std::memory_order_release);
    (void)prev; // Ignore overwrites; main thread consumes once
}

int ResonanceServer::consume_pending_reinit_frame_size() {
    return pending_reinit_frame_size_.exchange(0, std::memory_order_acq_rel);
}

void ResonanceServer::set_listener_valid(bool valid) {
    pending_listener_valid.store(valid);
}

void ResonanceServer::notify_listener_changed() {
    // API compatibility with Steam Audio. ResonanceRuntime updates listener every frame from camera.
    // Use when listener is created/swapped and you drive it manually via update_listener.
}

void ResonanceServer::notify_listener_changed_to(Node* listener_node) {
    if (!listener_node || !_ctx())
        return;
    Node3D* n3d = Object::cast_to<Node3D>(listener_node);
    if (!n3d)
        return;
    Transform3D tr = n3d->get_global_transform();
    Vector3 pos = tr.origin;
    Vector3 forward = -tr.basis.get_column(2);
    Vector3 up = tr.basis.get_column(1);
    update_listener(pos, forward, up);
}

void ResonanceServer::update_listener(Vector3 pos, Vector3 dir, Vector3 up) {
    if (!_ctx())
        return;

    // Orthonormalize basis for safety; use safe_unit_vector to avoid NaN from degenerate transforms
    Vector3 dir_n = ResonanceUtils::safe_unit_vector(dir, Vector3(0, 0, -1));
    Vector3 up_raw = ResonanceUtils::safe_unit_vector(up, Vector3(0, 1, 0));
    Vector3 right_n = ResonanceUtils::safe_unit_vector(dir_n.cross(up_raw), Vector3(1, 0, 0));
    Vector3 up_n = ResonanceUtils::safe_unit_vector(right_n.cross(dir_n), Vector3(0, 1, 0));

    IPLCoordinateSpace3 listener;
    listener.origin = ResonanceUtils::to_ipl_vector3(pos);
    listener.ahead = ResonanceUtils::to_ipl_vector3(dir_n);
    listener.up = ResonanceUtils::to_ipl_vector3(up_n);
    listener.right = ResonanceUtils::to_ipl_vector3(right_n);

    listener_coords_[1] = listener;
    new_listener_written_.store(true, std::memory_order_release);

    // FMOD Bridge: Update reverb source position to match listener.
    if (fmod_reverb_source_handle_ >= 0) {
        update_source(fmod_reverb_source_handle_, pos, 1.0f);
    }
}

// --- SOURCE API ---

int32_t ResonanceServer::create_source_handle(Vector3 pos, float radius) {
    if (!_ctx() || !simulator)
        return -1;
    IPLSourceSettings settings{};
    settings.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    if (pathing_enabled)
        settings.flags = static_cast<IPLSimulationFlags>(settings.flags | IPL_SIMULATIONFLAGS_PATHING);
    IPLSource src = nullptr;
    if (iplSourceCreate(simulator, &settings, &src) != IPL_STATUS_SUCCESS || !src) {
        ResonanceLog::error("ResonanceServer: iplSourceCreate failed (create_source_handle).");
        return -1;
    }
    {
        iplSourceAdd(src, simulator);
        iplSimulatorCommit(simulator);
        int32_t handle = source_manager.add_source(src);
        {
            std::lock_guard<std::mutex> lock(reflections_pending_mutex_);
            reflections_pending_handles_.insert(handle);
        }
        _update_source_internal(src, handle, pos, radius, Vector3(0, 0, -1), Vector3(0, 1, 0), 0.0f, 1.0f, true, false, 1.0f,
                                false, false, resonance::kDefaultOcclusionSamples, resonance::kDefaultTransmissionRays, 0, Vector3(0, 0, 0), 0.0f);
        iplSourceRelease(&src);
        return handle;
    }
}

void ResonanceServer::destroy_source_handle(int32_t handle) {
    if (handle < 0 || is_shutting_down_flag || !_ctx())
        return;
    IPLSource src = source_manager.get_source(handle);
    if (src) {
        if (simulator)
            iplSourceRemove(src, simulator);
        iplSourceRelease(&src);
    }
    {
        std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
        reverb_param_cache_write_.erase(handle);
        reverb_cache_dirty_.store(true);
    }
    {
        std::lock_guard<std::mutex> c_lock(reflection_cache_mutex_);
        reflection_param_cache_write_.erase(handle);
        reflection_cache_dirty_.store(true);
    }
    {
        std::lock_guard<std::mutex> c_lock(pathing_cache_mutex_);
        pathing_param_cache_write_.erase(handle);
        // Do NOT erase pathing_param_output_[handle] here: the audio thread may still hold path_params.shCoeffs
        // pointing to pathing_param_output_[handle].sh_coeffs.data() from a recent fetch_pathing_params.
        // Stale entries are cleared on full shutdown (_shutdown_steam_audio). Memory footprint is small.
        pathing_cache_dirty_.store(true);
    }
    // Do NOT erase from _source_attenuation_callback_data here: the worker may still run simulation
    // with inputs that reference our callback; erasing would invalidate ctx.data and cause use-after-free.
    // Maps are cleared in _shutdown_steam_audio after worker thread has joined.
    {
        std::lock_guard<std::mutex> lock(reflections_pending_mutex_);
        reflections_pending_handles_.erase(handle);
    }
    source_manager.remove_source(handle);
}

void ResonanceServer::update_source(int32_t handle, Vector3 pos, float radius,
                                    Vector3 source_forward, Vector3 source_up,
                                    float directivity_weight, float directivity_power, bool air_absorption_enabled,
                                    bool use_sim_distance_attenuation, float min_distance,
                                    bool path_validation_enabled, bool find_alternate_paths,
                                    int occlusion_samples, int num_transmission_rays,
                                    int baked_data_variation, Vector3 baked_endpoint_center, float baked_endpoint_radius,
                                    int32_t pathing_probe_batch_handle,
                                    int reflections_enabled_override,
                                    int pathing_enabled_override) {
    if (handle < 0)
        return;
    IPLSource src = source_manager.get_source(handle);
    if (src) {
        {
            std::lock_guard<std::mutex> lock(simulation_mutex);
            _update_source_internal(src, handle, pos, radius, source_forward, source_up,
                                    directivity_weight, directivity_power, air_absorption_enabled,
                                    use_sim_distance_attenuation, min_distance,
                                    path_validation_enabled, find_alternate_paths,
                                    occlusion_samples, num_transmission_rays,
                                    baked_data_variation, baked_endpoint_center, baked_endpoint_radius,
                                    pathing_probe_batch_handle, reflections_enabled_override, pathing_enabled_override);
        }
        iplSourceRelease(&src);
    }
}

void ResonanceServer::set_source_attenuation_callback_data(int32_t handle, int attenuation_mode, float min_distance, float max_distance, const PackedFloat32Array& curve_samples) {
    if (handle < 0)
        return;
    std::lock_guard<std::mutex> lock(_attenuation_callback_mutex);
    AttenuationCallbackData& d = _source_attenuation_callback_data[handle];
    d.mode = attenuation_mode;
    d.min_distance = min_distance;
    d.max_distance = max_distance;
    d.num_curve_samples = (!curve_samples.is_empty() && curve_samples.size() <= resonance::kAttenuationCurveSamples) ? curve_samples.size() : resonance::kAttenuationCurveSamples;
    for (int i = 0; i < d.num_curve_samples && i < curve_samples.size(); i++) {
        d.curve_samples[i] = curve_samples[i];
    }
}

void ResonanceServer::clear_source_attenuation_callback_data(int32_t handle) {
    if (handle < 0)
        return;
    std::lock_guard<std::mutex> lock(_attenuation_callback_mutex);
    _source_attenuation_callback_data.erase(handle);
    _source_attenuation_context.erase(handle);
}

static float IPLCALL _distance_attenuation_callback(IPLfloat32 distance, void* userData) {
    const ResonanceServer::AttenuationCallbackContext* ctx = static_cast<const ResonanceServer::AttenuationCallbackContext*>(userData);
    if (!ctx || !ctx->mutex || !ctx->data)
        return 1.0f;
    std::lock_guard<std::mutex> lock(*ctx->mutex);
    const ResonanceServer::AttenuationCallbackData* d = ctx->data;
    if (!d || d->max_distance <= d->min_distance)
        return 1.0f;
    if (distance <= d->min_distance)
        return (d->mode == 2 && d->num_curve_samples > 0) ? resonance::sanitize_audio_float(d->curve_samples[0]) : 1.0f;
    if (distance >= d->max_distance)
        return (d->mode == 2 && d->num_curve_samples > 0) ? resonance::sanitize_audio_float(d->curve_samples[d->num_curve_samples - 1]) : 0.0f;
    float t = (distance - d->min_distance) / (d->max_distance - d->min_distance);
    t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f
                                       : t;
    if (d->mode == 1)
        return 1.0f - t;
    if (d->mode == 2 && d->num_curve_samples > 1) {
        float idx = t * (d->num_curve_samples - 1);
        int i0 = (int)idx;
        int i1 = (i0 + 1 < d->num_curve_samples) ? i0 + 1 : i0;
        float frac = idx - (float)i0;
        float v = d->curve_samples[i0] * (1.0f - frac) + d->curve_samples[i1] * frac;
        return resonance::sanitize_audio_float(v);
    }
    return resonance::sanitize_audio_float(1.0f - t);
}

void ResonanceServer::_update_source_internal(IPLSource src, int32_t handle, Vector3 pos, float radius,
                                              Vector3 source_forward, Vector3 source_up,
                                              float directivity_weight, float directivity_power, bool air_absorption_enabled,
                                              bool use_sim_distance_attenuation, float min_distance,
                                              bool path_validation_enabled, bool find_alternate_paths,
                                              int occlusion_samples, int num_transmission_rays,
                                              int baked_data_variation, Vector3 baked_endpoint_center, float baked_endpoint_radius,
                                              int32_t pathing_probe_batch_handle,
                                              int reflections_enabled_override,
                                              int pathing_enabled_override) {
    if (!src || !_ctx())
        return;
    IPLSimulationInputs inputs{};
    bool enable_reflections = (reflections_enabled_override == -1) ? true : (reflections_enabled_override != 0);
    bool enable_pathing = (pathing_enabled_override == -1) ? pathing_enabled : (pathing_enabled_override != 0);
    IPLSimulationFlags sim_flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT);
    if (enable_reflections)
        sim_flags = static_cast<IPLSimulationFlags>(sim_flags | IPL_SIMULATIONFLAGS_REFLECTIONS);
    inputs.directFlags = (IPLDirectSimulationFlags)(IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
    if (use_sim_distance_attenuation)
        inputs.directFlags = (IPLDirectSimulationFlags)(inputs.directFlags | IPL_DIRECTSIMULATIONFLAGS_DISTANCEATTENUATION);
    if (air_absorption_enabled)
        inputs.directFlags = (IPLDirectSimulationFlags)(inputs.directFlags | IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION);
    if (directivity_weight != 0.0f || directivity_power != 1.0f)
        inputs.directFlags = (IPLDirectSimulationFlags)(inputs.directFlags | IPL_DIRECTSIMULATIONFLAGS_DIRECTIVITY);
    inputs.source.origin = ResonanceUtils::to_ipl_vector3(pos);
    Vector3 ahead_n = ResonanceUtils::safe_unit_vector(source_forward, Vector3(0, 0, -1));
    Vector3 up_raw = ResonanceUtils::safe_unit_vector(source_up, Vector3(0, 1, 0));
    Vector3 right_n = ResonanceUtils::safe_unit_vector(ahead_n.cross(up_raw), Vector3(1, 0, 0));
    Vector3 up_n = ResonanceUtils::safe_unit_vector(right_n.cross(ahead_n), Vector3(0, 1, 0));
    inputs.source.ahead = ResonanceUtils::to_ipl_vector3(ahead_n);
    inputs.source.up = ResonanceUtils::to_ipl_vector3(up_n);
    inputs.source.right = ResonanceUtils::to_ipl_vector3(right_n);
    inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
    inputs.directivity.dipoleWeight = directivity_weight;
    inputs.directivity.dipolePower = directivity_power;
    inputs.directivity.callback = nullptr;
    inputs.directivity.userData = nullptr;
    bool use_callback = false;
    AttenuationCallbackContext* callback_ctx = nullptr;
    {
        std::lock_guard<std::mutex> cb_lock(_attenuation_callback_mutex);
        auto it = _source_attenuation_callback_data.find(handle);
        if (it != _source_attenuation_callback_data.end() && (it->second.mode == 1 || it->second.mode == 2)) {
            use_callback = true;
            AttenuationCallbackContext& ctx = _source_attenuation_context[handle];
            ctx.mutex = &_attenuation_callback_mutex;
            ctx.data = &it->second;
            callback_ctx = &ctx;
        }
    }
    if (use_sim_distance_attenuation) {
        if (use_callback && callback_ctx) {
            inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_CALLBACK;
            inputs.distanceAttenuationModel.minDistance = callback_ctx->data->min_distance;
            inputs.distanceAttenuationModel.callback = _distance_attenuation_callback;
            inputs.distanceAttenuationModel.userData = callback_ctx;
            inputs.distanceAttenuationModel.dirty = IPL_FALSE;
        } else {
            inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
            inputs.distanceAttenuationModel.minDistance = min_distance;
            inputs.distanceAttenuationModel.callback = nullptr;
            inputs.distanceAttenuationModel.userData = nullptr;
        }
    } else {
        inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
        inputs.distanceAttenuationModel.callback = nullptr;
        inputs.distanceAttenuationModel.userData = nullptr;
    }
    inputs.occlusionType = (occlusion_type == 0) ? IPL_OCCLUSIONTYPE_RAYCAST : IPL_OCCLUSIONTYPE_VOLUMETRIC;
    inputs.occlusionRadius = radius;
    inputs.numOcclusionSamples = CLAMP(occlusion_samples, 1, simulation_settings.maxNumOcclusionSamples);
    inputs.numTransmissionRays = CLAMP(num_transmission_rays, 1, resonance::kMaxTransmissionRays);

    // Reflections: baked_data_variation -1=Realtime, 0=REVERB, 1=STATICSOURCE, 2=STATICLISTENER
    if (baked_data_variation == -1) {
        static int s_realtime_source_log_count = 0;
        if (s_realtime_source_log_count < 3) {
            String src_msg = "Source " + String::num_int64(handle) + " using Realtime reflections (baked=FALSE). Rays: " + String::num_int64(max_rays);
            UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] " + src_msg);
            s_realtime_source_log_count++;
        }
        inputs.baked = IPL_FALSE;
        inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
        inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
        inputs.bakedDataIdentifier.endpointInfluence.center = {0.0f, 0.0f, 0.0f};
        inputs.bakedDataIdentifier.endpointInfluence.radius = 0.0f;
    } else {
        inputs.baked = IPL_TRUE;
        inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
        if (baked_data_variation == 1) {
            inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_STATICSOURCE;
            inputs.bakedDataIdentifier.endpointInfluence.center = ResonanceUtils::to_ipl_vector3(baked_endpoint_center);
            inputs.bakedDataIdentifier.endpointInfluence.radius = (baked_endpoint_radius > 0.0f) ? baked_endpoint_radius : reverb_influence_radius;
        } else if (baked_data_variation == 2) {
            inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_STATICLISTENER;
            inputs.bakedDataIdentifier.endpointInfluence.center = ResonanceUtils::to_ipl_vector3(baked_endpoint_center);
            inputs.bakedDataIdentifier.endpointInfluence.radius = (baked_endpoint_radius > 0.0f) ? baked_endpoint_radius : reverb_influence_radius;
        } else {
            inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
            inputs.bakedDataIdentifier.endpointInfluence.center = {0.0f, 0.0f, 0.0f};
            inputs.bakedDataIdentifier.endpointInfluence.radius = 0.0f;
        }
    }

    inputs.reverbScale[0] = 1.0f;
    inputs.reverbScale[1] = 1.0f;
    inputs.reverbScale[2] = 1.0f;
    inputs.hybridReverbTransitionTime = hybrid_reverb_transition_time;
    inputs.hybridReverbOverlapPercent = hybrid_reverb_overlap_percent;

    if (enable_pathing && pathing_enabled) {
        IPLProbeBatch path_batch = _get_pathing_batch_for_source(pathing_probe_batch_handle);
        if (path_batch) {
            sim_flags = static_cast<IPLSimulationFlags>(sim_flags | IPL_SIMULATIONFLAGS_PATHING);
            inputs.pathingProbes = path_batch;
            inputs.pathingOrder = ambisonic_order;
            inputs.visRadius = pathing_vis_radius;
            inputs.visThreshold = pathing_vis_threshold;
            inputs.visRange = pathing_vis_range;
            // enableValidation/findAlternatePaths control path-around-obstacles (sound around corners).
            // Do NOT tie them to listener_valid—that would disable pathing whenever listener might be
            // unreachable. RunPathing crash (listener in geometry) is handled by SEH in the worker.
            inputs.enableValidation = path_validation_enabled ? IPL_TRUE : IPL_FALSE;
            inputs.findAlternatePaths = find_alternate_paths ? IPL_TRUE : IPL_FALSE;
            {
                std::lock_guard<std::mutex> d_lock(_pathing_deviation_mutex);
                inputs.deviationModel = (_pathing_deviation_callback_enabled && _pathing_deviation_model.callback) ? &_pathing_deviation_model : nullptr;
            }
            iplProbeBatchRelease(&path_batch);
        }
    }
    inputs.flags = sim_flags;

    iplSourceSetInputs(src, sim_flags, &inputs);
}

IPLSource ResonanceServer::get_source_from_handle(int32_t handle) {
    return source_manager.get_source(handle);
}

// --- CALCULATIONS ---

float ResonanceServer::calculate_distance_attenuation(Vector3 source_pos, Vector3 listener_pos, float min_dist, float max_dist) {
    if (!_ctx())
        return 1.0f;
    IPLDistanceAttenuationModel model{};
    model.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
    model.minDistance = min_dist;
    IPLVector3 src = ResonanceUtils::to_ipl_vector3(source_pos);
    IPLVector3 dst = ResonanceUtils::to_ipl_vector3(listener_pos);
    return iplDistanceAttenuationCalculate(_ctx(), src, dst, &model);
}

Vector3 ResonanceServer::calculate_air_absorption(Vector3 source_pos, Vector3 listener_pos) {
    if (!_ctx())
        return Vector3(1, 1, 1);
    IPLAirAbsorptionModel model{};
    model.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
    IPLVector3 src = ResonanceUtils::to_ipl_vector3(source_pos);
    IPLVector3 dst = ResonanceUtils::to_ipl_vector3(listener_pos);
    float air_abs[3] = {1.0f, 1.0f, 1.0f};
    iplAirAbsorptionCalculate(_ctx(), src, dst, &model, air_abs);
    return Vector3(air_abs[0], air_abs[1], air_abs[2]);
}

float ResonanceServer::calculate_directivity(Vector3 source_pos, Vector3 fwd, Vector3 up, Vector3 right, Vector3 listener_pos, float weight, float power) {
    if (!_ctx())
        return 1.0f;
    IPLDirectivity dSettings{};
    dSettings.dipoleWeight = weight;
    dSettings.dipolePower = power;
    IPLCoordinateSpace3 source_space{};
    source_space.origin = ResonanceUtils::to_ipl_vector3(source_pos);
    source_space.ahead = ResonanceUtils::to_ipl_vector3(fwd);
    source_space.up = ResonanceUtils::to_ipl_vector3(up);
    source_space.right = ResonanceUtils::to_ipl_vector3(right);
    IPLVector3 lst = ResonanceUtils::to_ipl_vector3(listener_pos);
    return iplDirectivityCalculate(_ctx(), source_space, lst, &dSettings);
}

// --- DATA FETCH ---

OcclusionData ResonanceServer::get_source_occlusion_data(int32_t handle) {
    OcclusionData result;
    result.occlusion = 0.0f;
    result.transmission[0] = 1.0f;
    result.transmission[1] = 1.0f;
    result.transmission[2] = 1.0f;
    result.air_absorption[0] = 1.0f;
    result.air_absorption[1] = 1.0f;
    result.air_absorption[2] = 1.0f;
    result.directivity = 1.0f;
    result.distance_attenuation = 1.0f;
    if (handle < 0 || !_ctx())
        return result;
    IPLSource src = source_manager.get_source(handle);
    if (!src)
        return result;

    {
        std::lock_guard<std::mutex> lock(simulation_mutex);
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
        result.occlusion = outputs.direct.occlusion;
        result.transmission[0] = outputs.direct.transmission[0];
        result.transmission[1] = outputs.direct.transmission[1];
        result.transmission[2] = outputs.direct.transmission[2];
        result.air_absorption[0] = outputs.direct.airAbsorption[0];
        result.air_absorption[1] = outputs.direct.airAbsorption[1];
        result.air_absorption[2] = outputs.direct.airAbsorption[2];
        result.directivity = outputs.direct.directivity;
        result.distance_attenuation = outputs.direct.distanceAttenuation;
    }
    iplSourceRelease(&src);
    return result;
}

bool ResonanceServer::fetch_reverb_params(int32_t handle, IPLReflectionEffectParams& out_params) {
    if (handle < 0 || !_ctx())
        return false;
    IPLSource src = source_manager.get_source(handle);
    if (!src)
        return false;
    bool result = false;

    // Steam Audio can return non-zero reverb times even with no probes and numRays=0 (e.g. scene-based
    // estimate or internal default). For reliable output: only treat as valid when we have a real
    // data source (probe batches for baked, or realtime rays).
    if (_uses_parametric_or_hybrid() || reflection_type == resonance::kReflectionTan) {
        if (max_rays == 0) {
            if (!probe_batch_registry_.has_any_batches()) {
                iplSourceRelease(&src);
                return false;
            }
        }
    }

    // For Parametric/Hybrid: avoid iplSourceGetOutputs until RunReflections has run at least once.
    // Before that, simulation_data initializes reverbTimes=0.0f; Steam Audio validation requires >0 for PARAMETRIC.
    if (_uses_parametric_or_hybrid() && !reflections_have_run_once_.load(std::memory_order_acquire)) {
        iplSourceRelease(&src);
        return false;
    }
    // Per-source: skip getOutputs(REFLECTIONS) until this source has been through at least one RunReflections.
    // New sources get reverbTimes=0 until the next run; calling getOutputs would trigger Steam Audio validation warning.
    if (_uses_parametric_or_hybrid()) {
        std::lock_guard<std::mutex> lock(reflections_pending_mutex_);
        if (reflections_pending_handles_.count(handle) != 0) {
            iplSourceRelease(&src);
            return false;
        }
    }

    // Convolution (0), Hybrid (2), TAN (3): use try_lock + cache fallback to avoid blocking audio thread.
    // Parametric (1): try_lock + cache fallback. RAII: unique_lock unlocks on scope exit or exception.
    std::unique_lock<std::mutex> lock(simulation_mutex, std::defer_lock);
    if (lock.try_lock()) {
        instrumentation_fetch_lock_ok.fetch_add(1, std::memory_order_relaxed);
        IPLSimulationOutputs outputs{};
        if (_uses_parametric_or_hybrid()) {
            outputs.reflections.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
        }
        iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
        bool has_convolution = (outputs.reflections.ir != nullptr);
        bool has_parametric = (outputs.reflections.reverbTimes[0] > 0 || outputs.reflections.reverbTimes[1] > 0 || outputs.reflections.reverbTimes[2] > 0);
        bool has_hybrid = (reflection_type == resonance::kReflectionHybrid && (has_convolution || outputs.reflections.reverbTimes[0] > 0));
        bool has_tan = (reflection_type == resonance::kReflectionTan && outputs.reflections.tanSlot >= 0 && _tan());
        if (has_convolution || (reflection_type == resonance::kReflectionParametric && has_parametric) || has_hybrid || has_tan) {
            out_params = outputs.reflections;
            for (int i = 0; i < resonance::kReverbBandCount; i++) {
                out_params.reverbTimes[i] = resonance::clamp_reverb_time(out_params.reverbTimes[i]);
                out_params.eq[i] = resonance::sanitize_audio_float(out_params.eq[i]);
            }
            out_params.delay = resonance::sanitize_audio_float(out_params.delay);
            // IR validation: reject convolution when ir metadata is invalid (avoids Steam Audio NaN warnings).
            if (has_convolution && out_params.ir != nullptr) {
                const int max_ir_samples = 480000; // ~10 s at 48 kHz
                const int max_ir_channels = 64;    // 7th-order ambisonics
                if (out_params.irSize <= 0 || out_params.irSize > max_ir_samples ||
                    out_params.numChannels <= 0 || out_params.numChannels > max_ir_channels) {
                    out_params.ir = nullptr;
                    has_convolution = false;
                    has_hybrid = (reflection_type == resonance::kReflectionHybrid && outputs.reflections.reverbTimes[0] > 0);
                }
            }
            // For Hybrid: Steam Audio validation requires ir non-null when type=HYBRID. Use PARAMETRIC when no IR.
            bool use_hybrid_type = (reflection_type == resonance::kReflectionHybrid && has_convolution && has_parametric);
            out_params.type = (reflection_type == resonance::kReflectionParametric) ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC : (reflection_type == resonance::kReflectionHybrid && use_hybrid_type) ? IPL_REFLECTIONEFFECTTYPE_HYBRID
                                                                                                                        : (reflection_type == resonance::kReflectionHybrid)                      ? IPL_REFLECTIONEFFECTTYPE_PARAMETRIC
                                                                                                                        : (reflection_type == resonance::kReflectionTan)                         ? IPL_REFLECTIONEFFECTTYPE_TAN
                                                                                                                                                                                                 : IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
            if (reflection_type == resonance::kReflectionTan)
                out_params.tanDevice = _tan();
            result = true;
            if (reflection_type == resonance::kReflectionConvolution && has_convolution) {
                reverb_convolution_valid_fetches.fetch_add(1, std::memory_order_relaxed);
            }
            if (reflection_type == resonance::kReflectionParametric && has_parametric) {
                std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
                for (int i = 0; i < resonance::kReverbBandCount; i++) {
                    reverb_param_cache_write_[handle].reverbTimes[i] = resonance::clamp_reverb_time(outputs.reflections.reverbTimes[i]);
                    reverb_param_cache_write_[handle].eq[i] = outputs.reflections.eq[i];
                }
                reverb_param_cache_write_[handle].valid = true;
                reverb_cache_dirty_.store(true);
            }
            if (_uses_convolution_or_hybrid_or_tan() && (has_convolution || has_hybrid || has_tan)) {
                std::lock_guard<std::mutex> c_lock(reflection_cache_mutex_);
                reflection_param_cache_write_[handle].params = out_params;
                reflection_param_cache_write_[handle].valid = true;
                reflection_cache_dirty_.store(true);
            }
        }
    } else if (reflection_type == resonance::kReflectionParametric) {
        if (reverb_cache_dirty_.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
            reverb_param_cache_read_.swap(reverb_param_cache_write_);
        }
        auto it = reverb_param_cache_read_.find(handle);
        if (it != reverb_param_cache_read_.end() && it->second.valid) {
            memset(&out_params, 0, sizeof(out_params));
            out_params.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
            for (int i = 0; i < resonance::kReverbBandCount; i++) {
                out_params.reverbTimes[i] = resonance::clamp_reverb_time(it->second.reverbTimes[i]);
                out_params.eq[i] = resonance::sanitize_audio_float(it->second.eq[i]);
            }
            result = true;
            instrumentation_fetch_cache_hit.fetch_add(1, std::memory_order_relaxed);
        } else {
            instrumentation_fetch_cache_miss.fetch_add(1, std::memory_order_relaxed);
        }
    } else if (_uses_convolution_or_hybrid_or_tan()) {
        if (reflection_cache_dirty_.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> c_lock(reflection_cache_mutex_);
            reflection_param_cache_read_.swap(reflection_param_cache_write_);
        }
        auto it = reflection_param_cache_read_.find(handle);
        if (it != reflection_param_cache_read_.end() && it->second.valid) {
            out_params = it->second.params;
            result = true;
            instrumentation_fetch_cache_hit.fetch_add(1, std::memory_order_relaxed);
        } else {
            instrumentation_fetch_cache_miss.fetch_add(1, std::memory_order_relaxed);
        }
    }

    iplSourceRelease(&src);
    return result;
}

bool ResonanceServer::fetch_pathing_params(int32_t handle, IPLPathEffectParams& out_params) {
    if (handle < 0 || !_ctx() || !pathing_enabled)
        return false;
    IPLSource src = source_manager.get_source(handle);
    if (!src)
        return false;
    bool result = false;

    // Use try_lock + cache fallback to avoid blocking audio thread (same pattern as fetch_reverb_params).
    // RAII: unique_lock unlocks on scope exit or exception.
    std::unique_lock<std::mutex> lock(simulation_mutex, std::defer_lock);
    if (lock.try_lock()) {
        IPLSimulationOutputs outputs{};
        iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_PATHING, &outputs);
        if (outputs.pathing.shCoeffs != nullptr) {
            int order = outputs.pathing.order;
            int sh_count = (order >= 0) ? (order + 1) * (order + 1) : 0;
            if (sh_count > 0 && outputs.pathing.shCoeffs) {
                // Copy into stable pathing_param_output_ so out_params.shCoeffs stays valid after unlock.
                std::lock_guard<std::mutex> c_lock(pathing_cache_mutex_);
                CachedPathingParams& out_entry = pathing_param_output_[handle];
                out_entry.eqCoeffs[0] = outputs.pathing.eqCoeffs[0];
                out_entry.eqCoeffs[1] = outputs.pathing.eqCoeffs[1];
                out_entry.eqCoeffs[2] = outputs.pathing.eqCoeffs[2];
                out_entry.sh_coeffs.assign(outputs.pathing.shCoeffs, outputs.pathing.shCoeffs + sh_count);
                out_entry.order = order;
                out_entry.valid = true;
                out_params = outputs.pathing;
                out_params.shCoeffs = out_entry.sh_coeffs.data();

                // Cache for lock-miss fallback when audio thread misses lock next time.
                CachedPathingParams& entry = pathing_param_cache_write_[handle];
                entry.eqCoeffs[0] = outputs.pathing.eqCoeffs[0];
                entry.eqCoeffs[1] = outputs.pathing.eqCoeffs[1];
                entry.eqCoeffs[2] = outputs.pathing.eqCoeffs[2];
                entry.sh_coeffs = out_entry.sh_coeffs;
                entry.order = order;
                entry.valid = true;
                pathing_cache_dirty_.store(true);
                out_params.binaural = reverb_binaural ? IPL_TRUE : IPL_FALSE;
                out_params.hrtf = _hrtf();
                out_params.normalizeEQ = pathing_normalize_eq ? IPL_TRUE : IPL_FALSE;
                result = true;
            } else {
                // sh_count <= 0: shCoeffs would point to internal Steam buffer, which RunPathing overwrites after unlock.
                // Do not return result = true with unstable pointer; pathing is skipped for this source this frame.
                result = false;
            }
        } else {
            if (!pathing_no_data_warned) {
                pathing_no_data_warned = true;
                UtilityFunctions::push_warning("Nexus Resonance: Pathing enabled but no pathing data. Bake Pathing separately after Bake Probes (Editor: select ProbeVolume -> Bake Pathing).");
            }
        }
    } else {
        // Lock miss: use cached params if available. Copy into stable output buffer so pointer stays valid.
        std::lock_guard<std::mutex> c_lock(pathing_cache_mutex_);
        if (pathing_cache_dirty_.exchange(false, std::memory_order_acq_rel)) {
            pathing_param_cache_read_.swap(pathing_param_cache_write_);
        }
        auto it = pathing_param_cache_read_.find(handle);
        if (it != pathing_param_cache_read_.end() && it->second.valid && !it->second.sh_coeffs.empty()) {
            // listener is not set here; caller (ResonancePlayer) sets path_params.listener before process().
            CachedPathingParams& out_entry = pathing_param_output_[handle];
            out_entry.eqCoeffs[0] = it->second.eqCoeffs[0];
            out_entry.eqCoeffs[1] = it->second.eqCoeffs[1];
            out_entry.eqCoeffs[2] = it->second.eqCoeffs[2];
            out_entry.sh_coeffs = it->second.sh_coeffs;
            out_entry.order = it->second.order;
            out_entry.valid = true;
            memset(&out_params, 0, sizeof(out_params));
            for (int i = 0; i < resonance::kReverbBandCount; i++)
                out_params.eqCoeffs[i] = out_entry.eqCoeffs[i];
            out_params.shCoeffs = out_entry.sh_coeffs.data();
            out_params.order = out_entry.order;
            out_params.binaural = reverb_binaural ? IPL_TRUE : IPL_FALSE;
            out_params.hrtf = _hrtf();
            out_params.normalizeEQ = pathing_normalize_eq ? IPL_TRUE : IPL_FALSE;
            result = true;
        }
    }

    iplSourceRelease(&src);
    return result;
}

void ResonanceServer::set_pathing_deviation_callback(IPLDeviationCallback callback, void* userData) {
    std::lock_guard<std::mutex> sim_lock(simulation_mutex);
    std::lock_guard<std::mutex> lock(_pathing_deviation_mutex);
    if (callback) {
        _pathing_deviation_model.type = IPL_DEVIATIONTYPE_CALLBACK;
        _pathing_deviation_model.callback = callback;
        _pathing_deviation_model.userData = userData;
        _pathing_deviation_callback_enabled = true;
    } else {
        _pathing_deviation_model.type = IPL_DEVIATIONTYPE_DEFAULT;
        _pathing_deviation_model.callback = nullptr;
        _pathing_deviation_model.userData = nullptr;
        _pathing_deviation_callback_enabled = false;
    }
}

void ResonanceServer::clear_pathing_deviation_callback() {
    set_pathing_deviation_callback(nullptr, nullptr);
}

// --- SCENE I/O ---

void ResonanceServer::notify_geometry_changed(int triangle_delta) {
    if (!_ctx())
        return;
    bool should_mark_dirty = (triangle_delta != 0) || _should_run_throttled(geometry_update_throttle_counter, geometry_update_throttle);
    std::lock_guard<std::mutex> lock(simulation_mutex);
    global_triangle_count += triangle_delta;
    if (should_mark_dirty)
        scene_dirty = true;
}

void ResonanceServer::save_scene_data(String filename) {
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.save_scene_data(_ctx(), scene, filename);
}

void ResonanceServer::save_scene_obj(String file_base_name) {
    std::lock_guard<std::mutex> lock(simulation_mutex);
    if (!scene) {
        UtilityFunctions::push_warning("Nexus Resonance: No scene to export (save_scene_obj).");
        return;
    }
    String abs_path = file_base_name;
    if (file_base_name.begins_with("res://") || file_base_name.begins_with("user://")) {
        ProjectSettings* ps = ProjectSettings::get_singleton();
        if (ps)
            abs_path = ps->globalize_path(file_base_name);
    }
    // Steam Audio dumpObj writes the OBJ file to the given path as-is (no .obj appended).
    if (!abs_path.ends_with(".obj")) {
        abs_path = abs_path + ".obj";
    }
    CharString path = abs_path.utf8();
    iplSceneSaveOBJ(scene, path.get_data());
    Engine* eng = Engine::get_singleton();
    if (eng && eng->is_editor_hint()) {
        UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] Scene exported to OBJ (base: " + file_base_name + ").");
    }
}

void ResonanceServer::load_scene_data(String filename) {
    if (!_ctx() || !simulator)
        return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.load_scene_data(_ctx(), &scene, simulator, _scene_type(), _embree(), _radeon(), filename, &global_triangle_count);
}

Error ResonanceServer::export_static_scene_to_asset(Node* scene_root, const String& p_path) {
    return scene_manager_.export_static_scene_to_asset(scene_root, p_path);
}

Error ResonanceServer::export_static_scene_to_obj(Node* scene_root, const String& file_base_name) {
    return scene_manager_.export_static_scene_to_obj(scene_root, file_base_name);
}

int64_t ResonanceServer::get_static_scene_hash(Node* scene_root) {
    return scene_manager_.get_static_scene_hash(scene_root, [this](const PackedByteArray& pba) { return _hash_probe_data(pba); });
}

int64_t ResonanceServer::get_geometry_asset_hash(const Ref<ResonanceGeometryAsset>& p_asset) const {
    if (!p_asset.is_valid() || !p_asset->is_valid())
        return 0;
    PackedByteArray data = p_asset->get_mesh_data();
    return static_cast<int64_t>(_hash_probe_data(data));
}

// --- BAKING & PROBES ---

uint64_t ResonanceServer::_hash_probe_data(const PackedByteArray& pba) {
    const uint8_t* ptr = pba.ptr();
    return resonance::fnv1a_hash(ptr, static_cast<size_t>(pba.size()));
}

PackedVector3Array ResonanceServer::generate_manual_grid(const Transform3D& tr, Vector3 extents, float spacing,
                                                         int generation_type, float height_above_floor) {
    return baker.generate_manual_grid(tr, extents, spacing, generation_type, height_above_floor);
}

PackedVector3Array ResonanceServer::generate_probes_scene_aware(const Transform3D& volume_transform, Vector3 extents,
                                                                float spacing, int generation_type, float height_above_floor) {
    PackedVector3Array out;
    if (!_ctx())
        return out;
    if (generation_type != ResonanceBaker::GEN_CENTROID && generation_type != ResonanceBaker::GEN_UNIFORM_FLOOR)
        return out;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    if (scene_dirty) {
        iplSceneCommit(scene);
        scene_dirty = false;
    }
    IPLScene temp_scene = nullptr;
    IPLStaticMesh temp_mesh = nullptr;
    IPLScene bake_scene = _prepare_bake_scene(&temp_scene, &temp_mesh);
    if (!bake_scene)
        return out;
    IPLProbeArray probeArray = nullptr;
    if (iplProbeArrayCreate(_ctx(), &probeArray) != IPL_STATUS_SUCCESS) {
        ResonanceLog::error("ResonanceServer: iplProbeArrayCreate failed (generate_probes_scene_aware).");
        return out;
    }
    IPLProbeGenerationParams genParams{};
    genParams.type = (generation_type == ResonanceBaker::GEN_CENTROID) ? IPL_PROBEGENERATIONTYPE_CENTROID : IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR;
    genParams.spacing = spacing;
    genParams.height = height_above_floor;
    genParams.transform = ResonanceUtils::create_volume_transform_rotated(volume_transform, extents);
    iplProbeArrayGenerateProbes(probeArray, bake_scene, &genParams);
    int num_probes = iplProbeArrayGetNumProbes(probeArray);
    for (int i = 0; i < num_probes; i++) {
        IPLSphere sphere = iplProbeArrayGetProbe(probeArray, i);
        out.push_back(ResonanceUtils::to_godot_vector3(sphere.center));
    }
    iplProbeArrayRelease(&probeArray);
    if (temp_mesh) {
        iplStaticMeshRemove(temp_mesh, temp_scene);
        iplStaticMeshRelease(&temp_mesh);
    }
    if (temp_scene)
        iplSceneRelease(&temp_scene);
    return out;
}

void ResonanceServer::set_bake_pipeline_pathing(bool p_pathing) {
    _bake_pipeline_pathing = p_pathing;
}

void ResonanceServer::set_bake_static_scene_asset(const Ref<ResonanceGeometryAsset>& p_asset) {
    _bake_static_scene_asset = p_asset;
}

void ResonanceServer::clear_static_scenes() {
    if (!_ctx() || !scene)
        return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    RuntimeSceneState state(_runtime_static_meshes, _runtime_static_triangle_count, _runtime_static_debug_mesh_ids,
                            &global_triangle_count, &scene_dirty, _runtime_static_sub_scenes, _runtime_static_instanced_meshes);
    scene_manager_.clear_static_scenes(scene, &ray_trace_debug_context_, state);
}

void ResonanceServer::add_static_scene_from_asset(const Ref<ResonanceGeometryAsset>& p_asset, const Transform3D& p_transform) {
    if (!_ctx() || !scene)
        return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    RuntimeSceneState state(_runtime_static_meshes, _runtime_static_triangle_count, _runtime_static_debug_mesh_ids,
                            &global_triangle_count, &scene_dirty, _runtime_static_sub_scenes, _runtime_static_instanced_meshes);
    scene_manager_.add_static_scene_from_asset(_ctx(), scene, p_asset, &ray_trace_debug_context_,
                                               wants_debug_reflection_viz(), state, p_transform, _scene_type(), _embree(), _radeon());
}

void ResonanceServer::load_static_scene_from_asset(const Ref<ResonanceGeometryAsset>& p_asset, const Transform3D& p_transform) {
    if (!_ctx() || !scene)
        return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    RuntimeSceneState state(_runtime_static_meshes, _runtime_static_triangle_count, _runtime_static_debug_mesh_ids,
                            &global_triangle_count, &scene_dirty, _runtime_static_sub_scenes, _runtime_static_instanced_meshes);
    scene_manager_.load_static_scene_from_asset(_ctx(), scene, p_asset, &ray_trace_debug_context_,
                                                wants_debug_reflection_viz(), state, p_transform, _scene_type(), _embree(), _radeon());
}

void ResonanceServer::set_bake_params(Dictionary params) {
    if (params.has("bake_num_rays"))
        _bake_num_rays = (int)params["bake_num_rays"];
    if (params.has("bake_num_bounces"))
        _bake_num_bounces = (int)params["bake_num_bounces"];
    if (params.has("bake_num_threads"))
        _bake_num_threads = (int)params["bake_num_threads"];
    if (params.has("bake_reflection_type"))
        _bake_reflection_type = (int)params["bake_reflection_type"];
    if (params.has("bake_pathing_vis_range"))
        _bake_pathing_vis_range = (float)params["bake_pathing_vis_range"];
    if (params.has("bake_pathing_path_range"))
        _bake_pathing_path_range = (float)params["bake_pathing_path_range"];
    if (params.has("bake_pathing_num_samples"))
        _bake_pathing_num_samples = (int)params["bake_pathing_num_samples"];
    if (params.has("bake_pathing_radius"))
        _bake_pathing_radius = (float)params["bake_pathing_radius"];
    if (params.has("bake_pathing_threshold"))
        _bake_pathing_threshold = (float)params["bake_pathing_threshold"];
}

int ResonanceServer::_get_bake_num_rays() const {
    if (_bake_num_rays >= 0)
        return _bake_num_rays;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps)
        return (int)ps->get_setting("audio/nexus_resonance/bake_num_rays", resonance::kBakeDefaultNumRays);
    return resonance::kBakeDefaultNumRays;
}

int ResonanceServer::_get_bake_num_bounces() const {
    if (_bake_num_bounces >= 0)
        return _bake_num_bounces;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps)
        return (int)ps->get_setting("audio/nexus_resonance/bake_num_bounces", resonance::kBakeDefaultNumBounces);
    return resonance::kBakeDefaultNumBounces;
}

int ResonanceServer::_get_bake_num_threads() const {
    if (_bake_num_threads >= 0)
        return _bake_num_threads;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps)
        return (int)ps->get_setting("audio/nexus_resonance/bake_num_threads", resonance::kBakeDefaultNumThreads);
    return resonance::kBakeDefaultNumThreads;
}

int ResonanceServer::_get_bake_reflection_type() const {
    if (_bake_reflection_type >= 0)
        return _bake_reflection_type;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps)
        return (int)ps->get_setting("audio/nexus_resonance/bake_reflection_type", 2);
    return 2;
}

float ResonanceServer::_get_bake_pathing_param(const char* key, float default_val) const {
    if (strcmp(key, "bake_pathing_vis_range") == 0 && _bake_pathing_vis_range >= 0)
        return _bake_pathing_vis_range;
    if (strcmp(key, "bake_pathing_path_range") == 0 && _bake_pathing_path_range >= 0)
        return _bake_pathing_path_range;
    if (strcmp(key, "bake_pathing_radius") == 0 && _bake_pathing_radius >= 0)
        return _bake_pathing_radius;
    if (strcmp(key, "bake_pathing_threshold") == 0 && _bake_pathing_threshold >= 0)
        return _bake_pathing_threshold;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (!ps)
        return default_val;
    String path = String("audio/nexus_resonance/") + key;
    return (float)ps->get_setting(path, default_val);
}

int ResonanceServer::_get_bake_pathing_num_samples() const {
    if (_bake_pathing_num_samples >= 0)
        return _bake_pathing_num_samples;
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (ps)
        return (int)ps->get_setting("audio/nexus_resonance/bake_pathing_num_samples", resonance::kBakePathingDefaultNumSamples);
    return resonance::kBakePathingDefaultNumSamples;
}

IPLScene ResonanceServer::_prepare_bake_scene(IPLScene* out_temp_scene, IPLStaticMesh* out_temp_mesh) {
    *out_temp_scene = nullptr;
    *out_temp_mesh = nullptr;
    if (_bake_static_scene_asset.is_valid() && _bake_static_scene_asset->is_valid() && _ctx()) {
        IPLSceneSettings sceneSettings{};
        sceneSettings.type = _scene_type();
        sceneSettings.embreeDevice = _embree();
        sceneSettings.radeonRaysDevice = _radeon();
        IPLScene temp = nullptr;
        if (iplSceneCreate(_ctx(), &sceneSettings, &temp) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceServer: iplSceneCreate failed (_prepare_bake_scene asset path).");
            return scene;
        }
        IPLSerializedObjectSettings serialSettings{};
        serialSettings.data = const_cast<IPLbyte*>(reinterpret_cast<const IPLbyte*>(_bake_static_scene_asset->get_data_ptr()));
        serialSettings.size = static_cast<IPLsize>(_bake_static_scene_asset->get_size());
        IPLSerializedObject serialObj = nullptr;
        if (iplSerializedObjectCreate(_ctx(), &serialSettings, &serialObj) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceServer: iplSerializedObjectCreate failed (_prepare_bake_scene).");
            iplSceneRelease(&temp);
            return scene;
        }
        IPLScopedRelease<IPLSerializedObject> serialGuard(serialObj, iplSerializedObjectRelease);
        IPLStaticMesh loadMesh = nullptr;
        if (iplStaticMeshLoad(temp, serialObj, nullptr, nullptr, &loadMesh) != IPL_STATUS_SUCCESS) {
            ResonanceLog::error("ResonanceServer: iplStaticMeshLoad failed (_prepare_bake_scene).");
            iplSceneRelease(&temp);
            return scene;
        }
        iplStaticMeshAdd(loadMesh, temp);
        iplSceneCommit(temp);
        *out_temp_scene = temp;
        *out_temp_mesh = loadMesh;
        return temp;
    }
    return scene;
}

bool ResonanceServer::_should_run_throttled(std::atomic<uint32_t>& counter, int throttle) {
    if (throttle <= 1)
        return true;
    uint32_t c = counter.fetch_add(1, std::memory_order_relaxed);
    return (c % (uint32_t)throttle) == 0;
}

bool ResonanceServer::_with_bake_scene(std::function<bool(IPLScene)> bake_fn) {
    std::lock_guard<std::mutex> lock(simulation_mutex);
    if (scene_dirty) {
        iplSceneCommit(scene);
        scene_dirty = false;
    }
    IPLScene temp_scene = nullptr;
    IPLStaticMesh temp_mesh = nullptr;
    IPLScene bake_scene = _prepare_bake_scene(&temp_scene, &temp_mesh);
    bool ok = bake_fn(bake_scene);
    if (temp_mesh) {
        iplStaticMeshRemove(temp_mesh, temp_scene);
        iplStaticMeshRelease(&temp_mesh);
    }
    if (temp_scene)
        iplSceneRelease(&temp_scene);
    return ok;
}

bool ResonanceServer::bake_manual_grid(const PackedVector3Array& points, Ref<ResonanceProbeData> data) {
    if (!_ctx() || !scene) {
        UtilityFunctions::push_error("Nexus Resonance Bake: Server not initialized.");
        return false;
    }
    if (!_bake_static_scene_asset.is_valid() || !_bake_static_scene_asset->is_valid()) {
        if (global_triangle_count <= 0) {
            UtilityFunctions::push_error("Nexus Resonance Bake: Scene not exported. Use Tools > Nexus Resonance > Export Static Scene before baking.");
            return false;
        }
    }
    int nb = _get_bake_num_bounces();
    int nr = _get_bake_num_rays();
    int bake_reflection = _get_bake_reflection_type();
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, &points, &data, nb, nr, bake_reflection, nt](IPLScene bake_scene) {
        return baker.bake_manual_grid(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(), points, nb, nr, bake_reflection, data, bake_progress_callback, this, _bake_pipeline_pathing, nt);
    });
}

bool ResonanceServer::bake_probes_for_volume(const Transform3D& volume_transform, Vector3 extents, float spacing,
                                             int generation_type, float height_above_floor, Ref<ResonanceProbeData> probe_data_res) {
    if (!_ctx() || !scene) {
        UtilityFunctions::push_error("Nexus Resonance Bake: Server not initialized.");
        return false;
    }
    if (!_bake_static_scene_asset.is_valid() || !_bake_static_scene_asset->is_valid()) {
        if (global_triangle_count <= 0) {
            UtilityFunctions::push_error("Nexus Resonance Bake: Scene not exported. Use Tools > Nexus Resonance > Export Static Scene before baking.");
            return false;
        }
    }
    int nb = _get_bake_num_bounces();
    int nr = _get_bake_num_rays();
    int bake_reflection = _get_bake_reflection_type();
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, volume_transform, extents, spacing, generation_type, height_above_floor, probe_data_res, nb, nr, bake_reflection, nt](IPLScene bake_scene) {
        if (generation_type == ResonanceBaker::GEN_CENTROID || generation_type == ResonanceBaker::GEN_UNIFORM_FLOOR) {
            return baker.bake_with_probe_array(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(),
                                               volume_transform, extents, spacing, generation_type, height_above_floor,
                                               nb, nr, bake_reflection, probe_data_res, bake_progress_callback, this, _bake_pipeline_pathing, nt);
        }
        PackedVector3Array points = baker.generate_manual_grid(volume_transform, extents, spacing, generation_type, height_above_floor);
        if (points.size() == 0)
            return false;
        return baker.bake_manual_grid(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(), points, nb, nr, bake_reflection, probe_data_res, bake_progress_callback, this, _bake_pipeline_pathing, nt);
    });
}

bool ResonanceServer::bake_pathing(Ref<ResonanceProbeData> data) {
    if (!_ctx() || !scene)
        return false;
    if (data.is_null() || data->get_data().is_empty())
        return false;
    float vis_range = _get_bake_pathing_param("bake_pathing_vis_range", resonance::kBakePathingDefaultVisRange);
    float path_range = _get_bake_pathing_param("bake_pathing_path_range", resonance::kBakePathingDefaultPathRange);
    int num_samples = _get_bake_pathing_num_samples();
    float radius = _get_bake_pathing_param("bake_pathing_radius", resonance::kBakePathingDefaultRadius);
    float threshold = _get_bake_pathing_param("bake_pathing_threshold", resonance::kBakePathingDefaultThreshold);
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, data, vis_range, path_range, num_samples, radius, threshold, nt](IPLScene bake_scene) {
        return baker.bake_pathing(_ctx(), bake_scene, data, vis_range, path_range, num_samples, radius, threshold, bake_progress_callback, this, nt);
    });
}

bool ResonanceServer::bake_static_source(Ref<ResonanceProbeData> data, Vector3 endpoint_position, float influence_radius) {
    if (!_ctx() || !scene)
        return false;
    if (data.is_null() || data->get_data().is_empty())
        return false;
    int nb = _get_bake_num_bounces();
    int nr = _get_bake_num_rays();
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, data, endpoint_position, influence_radius, nb, nr, nt](IPLScene bake_scene) {
        return baker.bake_static_source(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(),
                                        data, endpoint_position, influence_radius, nb, nr, bake_progress_callback, this, nt);
    });
}

bool ResonanceServer::bake_static_listener(Ref<ResonanceProbeData> data, Vector3 endpoint_position, float influence_radius) {
    if (!_ctx() || !scene)
        return false;
    if (data.is_null() || data->get_data().is_empty())
        return false;
    int nb = _get_bake_num_bounces();
    int nr = _get_bake_num_rays();
    int nt = _get_bake_num_threads();
    return _with_bake_scene([this, data, endpoint_position, influence_radius, nb, nr, nt](IPLScene bake_scene) {
        return baker.bake_static_listener(_ctx(), bake_scene, _scene_type(), _opencl(), _radeon(),
                                          data, endpoint_position, influence_radius, nb, nr, bake_progress_callback, this, nt);
    });
}

void ResonanceServer::cancel_reflections_bake() {
    if (_ctx())
        iplReflectionsBakerCancelBake(_ctx());
}

void ResonanceServer::cancel_pathing_bake() {
    if (_ctx())
        iplPathBakerCancelBake(_ctx());
}

int32_t ResonanceServer::load_probe_batch(Ref<ResonanceProbeData> data) {
    if (!_ctx() || data.is_null()) {
        UtilityFunctions::push_warning("Nexus Resonance: load_probe_batch skipped (no context or null data)");
        return -1;
    }
    PackedByteArray pba = data->get_data();
    if (pba.is_empty()) {
        UtilityFunctions::push_warning("Nexus Resonance: load_probe_batch skipped - probe_data.data is empty! Re-bake the probes.");
        return -1;
    }

    int baked_type = data->get_baked_reflection_type();
    if (baked_type >= 0 && baked_type <= 2) {
        if (!_is_reflection_type_compatible(baked_type)) {
            static std::set<std::pair<int, int>> s_warned_mismatch;
            auto key = std::make_pair(baked_type, reflection_type);
            if (s_warned_mismatch.find(key) == s_warned_mismatch.end()) {
                s_warned_mismatch.insert(key);
                const char* baked_names[] = {"Convolution", "Parametric", "Hybrid"};
                const char* runt_names[] = {"Convolution", "Parametric", "Hybrid", "TrueAudio Next"};
                String err = String("Probe data was baked as ") + baked_names[baked_type] +
                             " but runtime is set to " + runt_names[(reflection_type >= resonance::kReflectionConvolution && reflection_type <= resonance::kReflectionTan) ? reflection_type : resonance::kReflectionConvolution] +
                             ". Re-bake probes with matching reflection type or change runtime ReflectionType to match.";
                UtilityFunctions::push_error(err);
                Engine* eng = Engine::get_singleton();
                if (eng && eng->has_singleton("ResonanceLogger")) {
                    Dictionary log_data;
                    log_data["baked_reflection_type"] = baked_type;
                    log_data["runtime_reflection_type"] = reflection_type;
                    resonance_logger_log("validation", err.utf8().get_data(), log_data);
                }
            }
            return -1;
        }
    }

    // Skip pathing validation when called from bake_probes() mid-pipeline: pathing is baked
    // in a later step, so hash is 0 at this point. Validation runs again on reload after save.
    bool skip_pathing_check = _bake_pipeline_pathing;
    if (!skip_pathing_check && pathing_enabled && data->get_pathing_params_hash() == 0) {
        static bool s_warned_pathing_mismatch = false;
        if (!s_warned_pathing_mismatch) {
            s_warned_pathing_mismatch = true;
            String err = "Nexus Resonance: Pathing is enabled but probe data has no pathing baked. Bake Pathing in the Probe Volume editor (enable Pathing in bake_config, then Bake).";
            UtilityFunctions::push_warning(err);
            Engine* eng_path = Engine::get_singleton();
            if (eng_path && eng_path->has_singleton("ResonanceLogger")) {
                Dictionary log_data;
                log_data["pathing_enabled"] = true;
                log_data["pathing_params_hash"] = 0;
                resonance_logger_log("validation", err.utf8().get_data(), log_data);
            }
        }
        return -1;
    }

    uint64_t data_hash = _hash_probe_data(pba);
    int32_t handle = probe_batch_registry_.load_batch(_ctx(), simulator, &simulation_mutex, data, data_hash);
    return handle;
}

void ResonanceServer::_clear_all_param_caches() {
    {
        std::lock_guard<std::mutex> c_lock(reverb_cache_mutex_);
        reverb_param_cache_write_.clear();
        reverb_cache_dirty_.store(true);
    }
    {
        std::lock_guard<std::mutex> r_lock(reflection_cache_mutex_);
        reflection_param_cache_write_.clear();
        reflection_cache_dirty_.store(true);
    }
    {
        std::lock_guard<std::mutex> p_lock(pathing_cache_mutex_);
        pathing_param_cache_write_.clear();
        pathing_param_output_.clear();
        pathing_cache_dirty_.store(true);
    }
}

void ResonanceServer::remove_probe_batch(int32_t handle) {
    if (handle < 0 || is_shutting_down_flag || !_ctx() || !simulator)
        return;
    probe_batch_registry_.remove_batch(handle, simulator, &simulation_mutex);
    _clear_all_param_caches();
}

IPLProbeBatch ResonanceServer::_get_pathing_batch_for_source(int32_t preferred_handle) {
    return probe_batch_registry_.get_pathing_batch(preferred_handle);
}

bool ResonanceServer::_uses_convolution_or_hybrid_or_tan() const {
    return reflection_type == resonance::kReflectionConvolution || reflection_type == resonance::kReflectionHybrid || reflection_type == resonance::kReflectionTan;
}

bool ResonanceServer::_uses_parametric_or_hybrid() const {
    return reflection_type == resonance::kReflectionParametric || reflection_type == resonance::kReflectionHybrid;
}

bool ResonanceServer::_is_reflection_type_compatible(int baked_type) const {
    return (baked_type == 2) ||
           (baked_type == 0 && _uses_convolution_or_hybrid_or_tan()) ||
           (baked_type == 1 && _uses_parametric_or_hybrid());
}

bool ResonanceServer::_is_batch_compatible_with_config(int32_t handle) const {
    return probe_batch_registry_.is_compatible(handle, reflection_type, pathing_enabled);
}

int ResonanceServer::revalidate_probe_batches_with_config() {
    int n = probe_batch_registry_.revalidate_with_config(simulator, &simulation_mutex, reflection_type, pathing_enabled);
    if (n > 0) {
        _clear_all_param_caches();
    }
    return n;
}

void ResonanceServer::clear_probe_batches() {
    if (is_shutting_down_flag || !_ctx())
        return;
    probe_batch_registry_.clear_batches(simulator, &simulation_mutex);
    _clear_all_param_caches();
}

// --- SETTERS ---
void ResonanceServer::set_debug_occlusion(bool p_enabled) { debug_occlusion = p_enabled; }
bool ResonanceServer::is_debug_occlusion_enabled() const { return debug_occlusion; }
void ResonanceServer::set_debug_reflections(bool p_enabled) { debug_reflections = p_enabled; }
bool ResonanceServer::is_debug_reflections_enabled() const { return debug_reflections; }
void ResonanceServer::set_debug_pathing(bool p_enabled) { debug_pathing = p_enabled; }
bool ResonanceServer::is_debug_pathing_enabled() const { return debug_pathing; }

Array ResonanceServer::get_pathing_visualization_segments() {
    Array result;
    std::lock_guard<std::mutex> lock(pathing_vis_mutex);
    for (const PathVisSegment& seg : pathing_vis_segments) {
        Dictionary d;
        d["from"] = seg.from;
        d["to"] = seg.to;
        d["occluded"] = seg.occluded;
        result.push_back(d);
    }
    return result;
}

Array ResonanceServer::get_ray_debug_segments() {
    Array result;
    if (!wants_debug_reflection_viz())
        return result;
    IPLCoordinateSpace3 listener = get_current_listener_coords();
    IPLVector3 origin = listener.origin;
    ray_trace_debug_context_.trace_reflection_rays_for_viz(origin, max_rays, resonance::kRayDebugMaxDistance, result);
    return result;
}

Array ResonanceServer::get_ray_debug_segments_at(Vector3 origin) {
    Array result;
    if (!wants_debug_reflection_viz())
        return result;
    IPLVector3 o;
    o.x = origin.x;
    o.y = origin.y;
    o.z = origin.z;
    ray_trace_debug_context_.trace_reflection_rays_for_viz(o, max_rays, resonance::kRayDebugMaxDistance, result);
    return result;
}

bool ResonanceServer::uses_custom_ray_tracer() const {
    return _scene_type() == IPL_SCENETYPE_CUSTOM;
}

int ResonanceServer::register_debug_mesh(const std::vector<IPLVector3>& vertices,
                                         const std::vector<IPLTriangle>& triangles,
                                         const IPLint32* material_indices, const IPLMatrix4x4* transform, const IPLMaterial* material) {
    return ray_trace_debug_context_.register_mesh(vertices, triangles, material_indices, transform, material);
}

void ResonanceServer::reset_reverb_bus_instrumentation() {
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
}

Dictionary ResonanceServer::get_reverb_bus_instrumentation() const {
    Dictionary d;
    d["effect_process_calls"] = (int64_t)reverb_effect_process_calls.load(std::memory_order_relaxed);
    d["effect_mixer_null"] = (int64_t)reverb_effect_mixer_null.load(std::memory_order_relaxed);
    d["effect_success"] = (int64_t)reverb_effect_success.load(std::memory_order_relaxed);
    d["effect_frames_written"] = (int64_t)reverb_effect_frames_written.load(std::memory_order_relaxed);
    d["effect_output_peak"] = reverb_effect_output_peak.load(std::memory_order_relaxed);
    d["mixer_feed_count"] = (int64_t)reverb_mixer_feed_count.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> m_lock(mixer_access_mutex);
        d["mixer_exists"] = (reflection_mixer_[0] != nullptr || reflection_mixer_[1] != nullptr);
    }
    d["reflection_type"] = reflection_type;
    d["convolution_valid_fetches"] = (int64_t)reverb_convolution_valid_fetches.load(std::memory_order_relaxed);
    d["convolution_feed_ir_null"] = (int64_t)reverb_convolution_feed_ir_null.load(std::memory_order_relaxed);
    d["convolution_gain_min"] = reverb_convolution_gain_min.load(std::memory_order_relaxed);
    d["convolution_gain_max"] = reverb_convolution_gain_max.load(std::memory_order_relaxed);
    d["convolution_input_rms_max"] = reverb_convolution_input_rms_max.load(std::memory_order_relaxed);
    d["fetch_lock_ok"] = (int64_t)instrumentation_fetch_lock_ok.load(std::memory_order_relaxed);
    d["fetch_cache_hit"] = (int64_t)instrumentation_fetch_cache_hit.load(std::memory_order_relaxed);
    d["fetch_cache_miss"] = (int64_t)instrumentation_fetch_cache_miss.load(std::memory_order_relaxed);
    return d;
}

void ResonanceServer::record_convolution_feed(bool ir_non_null, float reverb_gain, float input_rms) {
    if (!ir_non_null)
        reverb_convolution_feed_ir_null.fetch_add(1, std::memory_order_relaxed);
    float gmin = reverb_convolution_gain_min.load(std::memory_order_relaxed);
    if (reverb_gain < gmin)
        reverb_convolution_gain_min.store(reverb_gain, std::memory_order_relaxed);
    float gmax = reverb_convolution_gain_max.load(std::memory_order_relaxed);
    if (reverb_gain > gmax)
        reverb_convolution_gain_max.store(reverb_gain, std::memory_order_relaxed);
    float rmax = reverb_convolution_input_rms_max.load(std::memory_order_relaxed);
    if (input_rms > rmax)
        reverb_convolution_input_rms_max.store(input_rms, std::memory_order_relaxed);
}

void ResonanceServer::update_reverb_effect_instrumentation(bool mixer_null, bool success, int32_t frames_written, float output_peak) {
    reverb_effect_process_calls.fetch_add(1, std::memory_order_relaxed);
    if (mixer_null)
        reverb_effect_mixer_null.fetch_add(1, std::memory_order_relaxed);
    if (success) {
        reverb_effect_success.fetch_add(1, std::memory_order_relaxed);
        reverb_effect_frames_written.fetch_add(static_cast<uint64_t>(frames_written), std::memory_order_relaxed);
        float prev = reverb_effect_output_peak.load(std::memory_order_relaxed);
        if (output_peak > prev)
            reverb_effect_output_peak.store(output_peak, std::memory_order_relaxed);
    }
}

void ResonanceServer::unregister_debug_mesh(int mesh_id) {
    ray_trace_debug_context_.unregister_mesh(mesh_id);
}

void ResonanceServer::set_output_direct_enabled(bool p_enabled) { output_direct_enabled = p_enabled; }
bool ResonanceServer::is_output_direct_enabled() const { return output_direct_enabled; }
void ResonanceServer::set_output_reverb_enabled(bool p_enabled) { output_reverb_enabled = p_enabled; }
bool ResonanceServer::is_output_reverb_enabled() const { return output_reverb_enabled; }
void ResonanceServer::set_reverb_influence_radius(float p_radius) { reverb_influence_radius = std::max(1.0f, p_radius); }
float ResonanceServer::get_reverb_influence_radius() const { return reverb_influence_radius; }
void ResonanceServer::set_reverb_max_distance(float p_dist) { reverb_max_distance = std::max(0.0f, p_dist); }
float ResonanceServer::get_reverb_max_distance() const { return reverb_max_distance; }
void ResonanceServer::set_reverb_transmission_amount(float p_amount) { reverb_transmission_amount = std::max(0.0f, std::min(1.0f, p_amount)); }
float ResonanceServer::get_reverb_transmission_amount() const { return reverb_transmission_amount; }
void ResonanceServer::set_perspective_correction_enabled(bool p_enabled) { perspective_correction_enabled = p_enabled; }
bool ResonanceServer::is_perspective_correction_enabled() const { return perspective_correction_enabled; }
void ResonanceServer::set_perspective_correction_factor(float p_factor) { perspective_correction_factor = std::max(0.1f, std::min(3.0f, p_factor)); }
float ResonanceServer::get_perspective_correction_factor() const { return perspective_correction_factor; }

bool ResonanceServer::use_reverb_binaural() const {
    return reverb_binaural && _hrtf() != nullptr;
}

void ResonanceServer::emit_bake_progress(float progress) {
    emit_signal("bake_progress", progress);
}

void ResonanceServer::_bind_methods() {
    ADD_SIGNAL(MethodInfo("bake_progress", PropertyInfo(Variant::FLOAT, "progress")));
    ClassDB::bind_method(D_METHOD("init_audio_engine", "config"), &ResonanceServer::init_audio_engine);
    ClassDB::bind_method(D_METHOD("reinit_audio_engine", "config"), &ResonanceServer::reinit_audio_engine);

    ClassDB::bind_method(D_METHOD("get_version"), &ResonanceServer::get_version);
    ClassDB::bind_method(D_METHOD("is_initialized"), &ResonanceServer::is_initialized);
    ClassDB::bind_method(D_METHOD("is_simulating"), &ResonanceServer::is_simulating);
    ClassDB::bind_method(D_METHOD("get_sample_rate"), &ResonanceServer::get_sample_rate);
    ClassDB::bind_method(D_METHOD("get_audio_frame_size"), &ResonanceServer::get_audio_frame_size);
    ClassDB::bind_method(D_METHOD("get_audio_frame_size_was_auto"), &ResonanceServer::get_audio_frame_size_was_auto);
    ClassDB::bind_method(D_METHOD("consume_pending_reinit_frame_size"), &ResonanceServer::consume_pending_reinit_frame_size);
    ClassDB::bind_method(D_METHOD("update_listener", "pos", "dir", "up"), &ResonanceServer::update_listener);
    ClassDB::bind_method(D_METHOD("set_listener_valid", "valid"), &ResonanceServer::set_listener_valid);
    ClassDB::bind_method(D_METHOD("notify_listener_changed"), &ResonanceServer::notify_listener_changed);
    ClassDB::bind_method(D_METHOD("notify_listener_changed_to", "listener_node"), &ResonanceServer::notify_listener_changed_to);
    ClassDB::bind_method(D_METHOD("tick", "delta"), &ResonanceServer::tick);

    // Probes
    ClassDB::bind_method(D_METHOD("generate_manual_grid", "tr", "ext", "sp", "generation_type", "height_above_floor"),
                         &ResonanceServer::generate_manual_grid, DEFVAL(2), DEFVAL(1.5));
    ClassDB::bind_method(D_METHOD("generate_probes_scene_aware", "volume_transform", "extents", "spacing", "generation_type", "height_above_floor"),
                         &ResonanceServer::generate_probes_scene_aware, DEFVAL(1), DEFVAL(1.5));
    ClassDB::bind_method(D_METHOD("bake_manual_grid", "pts", "dat"), &ResonanceServer::bake_manual_grid);
    ClassDB::bind_method(D_METHOD("set_bake_params", "params"), &ResonanceServer::set_bake_params);
    ClassDB::bind_method(D_METHOD("set_bake_static_scene_asset", "p_asset"), &ResonanceServer::set_bake_static_scene_asset);
    ClassDB::bind_method(D_METHOD("load_static_scene_from_asset", "p_asset", "p_transform"), &ResonanceServer::load_static_scene_from_asset, DEFVAL(Transform3D()));
    ClassDB::bind_method(D_METHOD("add_static_scene_from_asset", "p_asset", "p_transform"), &ResonanceServer::add_static_scene_from_asset, DEFVAL(Transform3D()));
    ClassDB::bind_method(D_METHOD("clear_static_scenes"), &ResonanceServer::clear_static_scenes);
    ClassDB::bind_method(D_METHOD("set_bake_pipeline_pathing", "pathing"), &ResonanceServer::set_bake_pipeline_pathing);
    ClassDB::bind_method(D_METHOD("bake_probes_for_volume", "volume_transform", "extents", "spacing", "generation_type", "height_above_floor", "probe_data"),
                         &ResonanceServer::bake_probes_for_volume);
    ClassDB::bind_method(D_METHOD("bake_pathing", "dat"), &ResonanceServer::bake_pathing);
    ClassDB::bind_method(D_METHOD("bake_static_source", "dat", "endpoint_position", "influence_radius"), &ResonanceServer::bake_static_source);
    ClassDB::bind_method(D_METHOD("bake_static_listener", "dat", "endpoint_position", "influence_radius"), &ResonanceServer::bake_static_listener);
    ClassDB::bind_method(D_METHOD("save_scene_obj", "file_base_name"), &ResonanceServer::save_scene_obj);
    ClassDB::bind_method(D_METHOD("export_static_scene_to_asset", "scene_root", "path"), &ResonanceServer::export_static_scene_to_asset);
    ClassDB::bind_method(D_METHOD("export_static_scene_to_obj", "scene_root", "file_base_name"), &ResonanceServer::export_static_scene_to_obj);
    ClassDB::bind_method(D_METHOD("get_static_scene_hash", "scene_root"), &ResonanceServer::get_static_scene_hash);
    ClassDB::bind_method(D_METHOD("get_geometry_asset_hash", "asset"), &ResonanceServer::get_geometry_asset_hash);
    ClassDB::bind_method(D_METHOD("cancel_reflections_bake"), &ResonanceServer::cancel_reflections_bake);
    ClassDB::bind_method(D_METHOD("cancel_pathing_bake"), &ResonanceServer::cancel_pathing_bake);
    ClassDB::bind_method(D_METHOD("load_probe_batch", "dat"), &ResonanceServer::load_probe_batch);
    ClassDB::bind_method(D_METHOD("remove_probe_batch", "handle"), &ResonanceServer::remove_probe_batch);
    ClassDB::bind_method(D_METHOD("clear_probe_batches"), &ResonanceServer::clear_probe_batches);
    ClassDB::bind_method(D_METHOD("revalidate_probe_batches_with_config"), &ResonanceServer::revalidate_probe_batches_with_config);

    // Bind Setter/Getters
    ClassDB::bind_method(D_METHOD("set_debug_occlusion", "p_enabled"), &ResonanceServer::set_debug_occlusion);
    ClassDB::bind_method(D_METHOD("is_debug_occlusion_enabled"), &ResonanceServer::is_debug_occlusion_enabled);

    ClassDB::bind_method(D_METHOD("set_debug_reflections", "p_enabled"), &ResonanceServer::set_debug_reflections);
    ClassDB::bind_method(D_METHOD("is_debug_reflections_enabled"), &ResonanceServer::is_debug_reflections_enabled);
    ClassDB::bind_method(D_METHOD("set_debug_pathing", "p_enabled"), &ResonanceServer::set_debug_pathing);
    ClassDB::bind_method(D_METHOD("is_debug_pathing_enabled"), &ResonanceServer::is_debug_pathing_enabled);
    ClassDB::bind_method(D_METHOD("get_pathing_visualization_segments"), &ResonanceServer::get_pathing_visualization_segments);
    ClassDB::bind_method(D_METHOD("get_ray_debug_segments"), &ResonanceServer::get_ray_debug_segments);
    ClassDB::bind_method(D_METHOD("get_ray_debug_segments_at", "origin"), &ResonanceServer::get_ray_debug_segments_at);
    ClassDB::bind_method(D_METHOD("uses_custom_ray_tracer"), &ResonanceServer::uses_custom_ray_tracer);
    ClassDB::bind_method(D_METHOD("wants_debug_reflection_viz"), &ResonanceServer::wants_debug_reflection_viz);
    ClassDB::bind_method(D_METHOD("is_pathing_enabled"), &ResonanceServer::is_pathing_enabled);
    ClassDB::bind_method(D_METHOD("set_pathing_enabled", "p_enabled"), &ResonanceServer::set_pathing_enabled);

    ClassDB::bind_method(D_METHOD("set_output_direct_enabled", "p_enabled"), &ResonanceServer::set_output_direct_enabled);
    ClassDB::bind_method(D_METHOD("is_output_direct_enabled"), &ResonanceServer::is_output_direct_enabled);

    ClassDB::bind_method(D_METHOD("set_output_reverb_enabled", "p_enabled"), &ResonanceServer::set_output_reverb_enabled);
    ClassDB::bind_method(D_METHOD("is_output_reverb_enabled"), &ResonanceServer::is_output_reverb_enabled);

    ClassDB::bind_method(D_METHOD("set_reverb_influence_radius", "p_radius"), &ResonanceServer::set_reverb_influence_radius);
    ClassDB::bind_method(D_METHOD("get_reverb_influence_radius"), &ResonanceServer::get_reverb_influence_radius);
    ClassDB::bind_method(D_METHOD("set_reverb_max_distance", "p_dist"), &ResonanceServer::set_reverb_max_distance);
    ClassDB::bind_method(D_METHOD("get_reverb_max_distance"), &ResonanceServer::get_reverb_max_distance);
    ClassDB::bind_method(D_METHOD("set_reverb_transmission_amount", "p_amount"), &ResonanceServer::set_reverb_transmission_amount);
    ClassDB::bind_method(D_METHOD("get_reverb_transmission_amount"), &ResonanceServer::get_reverb_transmission_amount);
    ClassDB::bind_method(D_METHOD("set_perspective_correction_enabled", "p_enabled"), &ResonanceServer::set_perspective_correction_enabled);
    ClassDB::bind_method(D_METHOD("is_perspective_correction_enabled"), &ResonanceServer::is_perspective_correction_enabled);
    ClassDB::bind_method(D_METHOD("set_perspective_correction_factor", "p_factor"), &ResonanceServer::set_perspective_correction_factor);
    ClassDB::bind_method(D_METHOD("get_perspective_correction_factor"), &ResonanceServer::get_perspective_correction_factor);
    ClassDB::bind_method(D_METHOD("get_reflection_type"), &ResonanceServer::get_reflection_type);
    ClassDB::bind_method(D_METHOD("get_realtime_rays"), &ResonanceServer::get_realtime_rays);
    ClassDB::bind_method(D_METHOD("get_reverb_bus_instrumentation"), &ResonanceServer::get_reverb_bus_instrumentation);
    ClassDB::bind_method(D_METHOD("reset_reverb_bus_instrumentation"), &ResonanceServer::reset_reverb_bus_instrumentation);

    // Properties
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_occlusion"), "set_debug_occlusion", "is_debug_occlusion_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_reflections"), "set_debug_reflections", "is_debug_reflections_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_pathing"), "set_debug_pathing", "is_debug_pathing_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "output_direct"), "set_output_direct_enabled", "is_output_direct_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "output_reverb"), "set_output_reverb_enabled", "is_output_reverb_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_influence_radius", PROPERTY_HINT_RANGE, "1,50000,1"), "set_reverb_influence_radius", "get_reverb_influence_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_max_distance", PROPERTY_HINT_RANGE, "0,5000,1"), "set_reverb_max_distance", "get_reverb_max_distance");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_transmission_amount", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_reverb_transmission_amount", "get_reverb_transmission_amount");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "perspective_correction_enabled"), "set_perspective_correction_enabled", "is_perspective_correction_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "perspective_correction_factor", PROPERTY_HINT_RANGE, "0.5,2.0,0.1"), "set_perspective_correction_factor", "get_perspective_correction_factor");
}
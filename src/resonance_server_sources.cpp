#include "resonance_constants.h"
#include "resonance_log.h"
#include "resonance_math.h"
#include "resonance_server.h"
#include "resonance_utils.h"
#include <cstdint>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

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
    std::lock_guard<std::mutex> sim_lock(simulation_mutex);
    iplSourceAdd(src, simulator);
    iplSimulatorCommit(simulator);
    int32_t handle = source_manager.add_source(src);
    {
        std::lock_guard<std::mutex> pc(pathing_cache_mutex_);
        pathing_param_output_.erase(handle);
        pathing_param_cache_read_.erase(handle);
        pathing_param_cache_write_.erase(handle);
    }
    {
        std::lock_guard<std::mutex> lock(reflections_pending_mutex_);
        reflections_pending_handles_.insert(handle);
    }
    _update_source_internal(src, handle, pos, radius, Vector3(0, 0, -1), Vector3(0, 1, 0), 0.0f, 1.0f, true, false, 1.0f,
                            false, false, resonance::kDefaultOcclusionSamples, resonance::kDefaultTransmissionRays, 0, Vector3(0, 0, 0), 0.0f);
    iplSourceRelease(&src);
    return handle;
}

void ResonanceServer::_destroy_source_handle_under_simulation_lock(int32_t handle) {
    IPLSource src = source_manager.get_source(handle);
    if (src) {
        if (simulator)
            iplSourceRemove(src, simulator);
        iplSourceRelease(&src);
    }
    {
        std::lock_guard<std::recursive_mutex> cb_lock(_attenuation_callback_mutex);
        _source_attenuation_entries.erase(handle);
    }
    _source_update_snapshot_.erase(handle);
    realtime_reflection_log_once_handles_.erase(handle);
    source_manager.remove_source(handle);
}

void ResonanceServer::destroy_source_handle(int32_t handle) {
    if (handle < 0 || is_shutting_down_flag.load(std::memory_order_acquire) || !_ctx())
        return;
    {
        std::lock_guard<std::mutex> sim_lock(simulation_mutex);
        _destroy_source_handle_under_simulation_lock(handle);
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
    {
        std::lock_guard<std::mutex> lock(reflections_pending_mutex_);
        reflections_pending_handles_.erase(handle);
    }
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
    {
        std::lock_guard<std::mutex> lock(simulation_mutex);
        IPLSource src = source_manager.get_source(handle);
        if (!src)
            return;
        _update_source_internal(src, handle, pos, radius, source_forward, source_up,
                                directivity_weight, directivity_power, air_absorption_enabled,
                                use_sim_distance_attenuation, min_distance,
                                path_validation_enabled, find_alternate_paths,
                                occlusion_samples, num_transmission_rays,
                                baked_data_variation, baked_endpoint_center, baked_endpoint_radius,
                                pathing_probe_batch_handle, reflections_enabled_override, pathing_enabled_override);
        iplSourceRelease(&src);
    }
}

void ResonanceServer::set_source_attenuation_callback_data(int32_t handle, int attenuation_mode, float min_distance, float max_distance, const PackedFloat32Array& curve_samples) {
    if (handle < 0)
        return;
    if (!source_manager.has_handle(handle))
        return;
    std::lock_guard<std::recursive_mutex> lock(_attenuation_callback_mutex);
    auto& entry_ptr = _source_attenuation_entries[handle];
    if (!entry_ptr)
        entry_ptr = std::make_unique<AttenuationEntry>();
    if (!entry_ptr->data)
        entry_ptr->data = std::make_unique<AttenuationCallbackData>();
    AttenuationCallbackData& d = *entry_ptr->data;
    d.mode = attenuation_mode;
    d.min_distance = min_distance;
    d.max_distance = max_distance;
    const int64_t curve_sz = curve_samples.size();
    d.num_curve_samples = static_cast<int>((!curve_samples.is_empty() && curve_sz <= resonance::kAttenuationCurveSamples) ? curve_sz
                                                                                                                          : static_cast<int64_t>(resonance::kAttenuationCurveSamples));
    for (int i = 0; i < d.num_curve_samples && i < curve_samples.size(); i++) {
        d.curve_samples[i] = curve_samples[i];
    }
}

void ResonanceServer::clear_source_attenuation_callback_data(int32_t handle) {
    if (handle < 0 || !_ctx())
        return;
    std::lock_guard<std::mutex> sim_lock(simulation_mutex);
    std::lock_guard<std::recursive_mutex> cb_lock(_attenuation_callback_mutex);
    _source_attenuation_entries.erase(handle);
    IPLSource src = source_manager.get_source(handle);
    if (!src)
        return;
    auto snap_it = _source_update_snapshot_.find(handle);
    if (snap_it == _source_update_snapshot_.end() || !snap_it->second.valid) {
        iplSourceRelease(&src);
        return;
    }
    const SourceUpdateSnapshot& p = snap_it->second;
    _update_source_internal(src, handle, p.position, p.radius, p.source_forward, p.source_up, p.directivity_weight, p.directivity_power,
                            p.air_absorption_enabled, p.use_sim_distance_attenuation, p.min_distance, p.path_validation_enabled, p.find_alternate_paths,
                            p.occlusion_samples, p.num_transmission_rays, p.baked_data_variation, p.baked_endpoint_center, p.baked_endpoint_radius,
                            p.pathing_probe_batch_handle, p.reflections_enabled_override, p.pathing_enabled_override);
    iplSourceRelease(&src);
}

static float IPLCALL distance_attenuation_callback(IPLfloat32 distance, void* userData) {
    const ResonanceServer::AttenuationCallbackContext* ctx = static_cast<const ResonanceServer::AttenuationCallbackContext*>(userData);
    if (!ctx || !ctx->mutex || !ctx->data)
        return 1.0f;
    std::lock_guard<std::recursive_mutex> lock(*ctx->mutex);
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
        float idx = t * static_cast<float>(d->num_curve_samples - 1);
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
    SourceUpdateSnapshot& snap = _source_update_snapshot_[handle];
    snap.position = pos;
    snap.radius = radius;
    snap.source_forward = source_forward;
    snap.source_up = source_up;
    snap.directivity_weight = directivity_weight;
    snap.directivity_power = directivity_power;
    snap.air_absorption_enabled = air_absorption_enabled;
    snap.use_sim_distance_attenuation = use_sim_distance_attenuation;
    snap.min_distance = min_distance;
    snap.path_validation_enabled = path_validation_enabled;
    snap.find_alternate_paths = find_alternate_paths;
    snap.occlusion_samples = occlusion_samples;
    snap.num_transmission_rays = num_transmission_rays;
    snap.baked_data_variation = baked_data_variation;
    snap.baked_endpoint_center = baked_endpoint_center;
    snap.baked_endpoint_radius = baked_endpoint_radius;
    snap.pathing_probe_batch_handle = pathing_probe_batch_handle;
    snap.reflections_enabled_override = reflections_enabled_override;
    snap.pathing_enabled_override = pathing_enabled_override;
    snap.valid = true;
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
        std::lock_guard<std::recursive_mutex> cb_lock(_attenuation_callback_mutex);
        auto it = _source_attenuation_entries.find(handle);
        if (it != _source_attenuation_entries.end() && it->second && it->second->data) {
            AttenuationCallbackData* pdata = it->second->data.get();
            if (pdata->mode == 1 || pdata->mode == 2) {
                use_callback = true;
                AttenuationEntry& entry = *it->second;
                entry.ctx.mutex = &_attenuation_callback_mutex;
                entry.ctx.data = pdata;
                callback_ctx = &entry.ctx;
            }
        }
    }
    if (use_sim_distance_attenuation) {
        if (use_callback && callback_ctx) {
            inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_CALLBACK;
            inputs.distanceAttenuationModel.minDistance = callback_ctx->data->min_distance;
            inputs.distanceAttenuationModel.callback = distance_attenuation_callback;
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
        if (realtime_reflection_log_once_handles_.insert(handle).second) {
            String src_msg = "Source " + String::num_int64(handle) + " first realtime reflections update (baked=FALSE). Rays: " + String::num_int64(max_rays);
            UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] " + src_msg);
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

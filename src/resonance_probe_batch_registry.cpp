#include "resonance_probe_batch_registry.h"
#include "resonance_probe_data.h"
#include <godot_cpp/classes/engine.hpp>

namespace godot {

bool ResonanceProbeBatchRegistry::is_reflection_type_compatible(int baked_type, int reflection_type) {
    bool uses_conv = (reflection_type == 0 || reflection_type == 2 || reflection_type == 3);
    bool uses_param = (reflection_type == 1 || reflection_type == 2);
    return (baked_type == 2) ||
        (baked_type == 0 && uses_conv) ||
        (baked_type == 1 && uses_param);
}

int32_t ResonanceProbeBatchRegistry::load_batch(IPLContext ctx, IPLSimulator sim, std::mutex* sim_mutex,
    Ref<ResonanceProbeData> data, uint64_t data_hash,
    int /*reflection_type*/, bool /*pathing_enabled*/, bool /*skip_pathing_check*/,
    std::function<bool(int)> /*is_reflection_compatible*/) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = hash_to_handle_.find(data_hash);
        if (it != hash_to_handle_.end()) {
            int32_t existing = it->second;
            refcount_[existing]++;
            if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
                UtilityFunctions::print("Nexus Resonance: Reusing existing probe batch (duplicate data). Refcount: ", refcount_[existing]);
            }
            return existing;
        }
    }

    PackedByteArray pba = data->get_data();
    IPLSerializedObjectSettings sSettings{};
    sSettings.data = (IPLbyte*)pba.ptr();
    sSettings.size = pba.size();
    IPLSerializedObject sObj = nullptr;
    iplSerializedObjectCreate(ctx, &sSettings, &sObj);

    IPLProbeBatch batch = nullptr;
    IPLerror load_status = iplProbeBatchLoad(ctx, sObj, &batch);
    iplSerializedObjectRelease(&sObj);

    if (load_status != IPL_STATUS_SUCCESS) {
        UtilityFunctions::push_warning("Nexus Resonance: iplProbeBatchLoad failed (status=%d). Re-bake the probes.", (int)load_status);
        return -1;
    }

    iplProbeBatchCommit(batch);
    {
        std::lock_guard<std::mutex> lock(*sim_mutex);
        iplSimulatorAddProbeBatch(sim, batch);
        iplSimulatorCommit(sim);
    }
    int32_t handle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handle = probe_batch_manager_.add_batch(batch);
        hash_to_handle_[data_hash] = handle;
        handle_to_hash_[handle] = data_hash;
        refcount_[handle] = 1;
        handle_has_pathing_[handle] = (data->get_pathing_params_hash() > 0);
        handle_baked_refl_[handle] = data->get_baked_reflection_type();
    }
    UtilityFunctions::print("Nexus Resonance: Probe batch loaded successfully. Reverb simulation active.");
    return handle;
}

void ResonanceProbeBatchRegistry::remove_batch(int32_t handle, IPLSimulator sim, std::mutex* sim_mutex) {
    IPLProbeBatch batch = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ref_it = refcount_.find(handle);
        if (ref_it == refcount_.end()) return;
        ref_it->second--;
        if (ref_it->second > 0) return;
        refcount_.erase(ref_it);
        handle_has_pathing_.erase(handle);
        handle_baked_refl_.erase(handle);
        auto hash_it = handle_to_hash_.find(handle);
        if (hash_it != handle_to_hash_.end()) {
            hash_to_handle_.erase(hash_it->second);
            handle_to_hash_.erase(hash_it);
        }
        batch = probe_batch_manager_.take_batch(handle);
    }
    if (batch && sim) {
        std::lock_guard<std::mutex> lock(*sim_mutex);
        iplSimulatorRemoveProbeBatch(sim, batch);
        iplSimulatorCommit(sim);
        iplProbeBatchRelease(&batch);
    }
}

void ResonanceProbeBatchRegistry::clear_batches(IPLSimulator sim, std::mutex* sim_mutex) {
    std::vector<IPLProbeBatch> batches;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hash_to_handle_.clear();
        handle_to_hash_.clear();
        refcount_.clear();
        handle_has_pathing_.clear();
        handle_baked_refl_.clear();
        probe_batch_manager_.get_all_batches(batches);
    }
    if (batches.empty() || !sim) return;
    std::lock_guard<std::mutex> lock(*sim_mutex);
    for (IPLProbeBatch batch : batches) {
        if (batch) iplSimulatorRemoveProbeBatch(sim, batch);
        if (batch) iplProbeBatchRelease(&batch);
    }
    iplSimulatorCommit(sim);
}

int ResonanceProbeBatchRegistry::revalidate_with_config(IPLSimulator sim, std::mutex* sim_mutex,
    int reflection_type, bool pathing_enabled) {
    std::vector<int32_t> to_remove;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : handle_to_hash_) {
            int32_t handle = kv.first;
            if (!is_compatible(handle, reflection_type, pathing_enabled)) to_remove.push_back(handle);
        }
    }
    for (int32_t h : to_remove) remove_batch(h, sim, sim_mutex);
    return (int)to_remove.size();
}

IPLProbeBatch ResonanceProbeBatchRegistry::get_pathing_batch(int32_t preferred_handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (preferred_handle >= 0 && handle_to_hash_.count(preferred_handle) &&
        handle_has_pathing_.count(preferred_handle) && handle_has_pathing_.at(preferred_handle)) {
        return probe_batch_manager_.get_batch(preferred_handle);
    }
    for (const auto& kv : handle_to_hash_) {
        int32_t handle = kv.first;
        if (handle_has_pathing_.count(handle) && handle_has_pathing_.at(handle)) {
            return probe_batch_manager_.get_batch(handle);
        }
    }
    return nullptr;
}

bool ResonanceProbeBatchRegistry::is_compatible(int32_t handle, int reflection_type, bool pathing_enabled) const {
    int baked_type = -1;
    bool has_pathing = false;
    auto refl_it = handle_baked_refl_.find(handle);
    if (refl_it != handle_baked_refl_.end()) baked_type = refl_it->second;
    auto path_it = handle_has_pathing_.find(handle);
    if (path_it != handle_has_pathing_.end()) has_pathing = path_it->second;

    if (baked_type >= 0 && baked_type <= 2) {
        if (!is_reflection_type_compatible(baked_type, reflection_type)) return false;
    }
    if (pathing_enabled && !has_pathing) return false;
    return true;
}

bool ResonanceProbeBatchRegistry::has_any_batches() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !handle_to_hash_.empty();
}

void ResonanceProbeBatchRegistry::get_all_batches_for_shutdown(std::vector<IPLProbeBatch>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    hash_to_handle_.clear();
    handle_to_hash_.clear();
    refcount_.clear();
    handle_has_pathing_.clear();
    handle_baked_refl_.clear();
    probe_batch_manager_.get_all_batches(out);
}

} // namespace godot

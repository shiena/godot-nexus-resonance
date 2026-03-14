#ifndef RESONANCE_PROBE_BATCH_REGISTRY_H
#define RESONANCE_PROBE_BATCH_REGISTRY_H

#include <cstdint>
#include <mutex>
#include <phonon.h>
#include <unordered_map>
#include <vector>

#include "handle_manager.h"
#include "resonance_probe_data.h"

namespace godot {

/// Manages probe batch hash mapping, refcount, and compatibility checks.
/// ResonanceServer delegates probe batch load/remove/clear/revalidate to this class.
class ResonanceProbeBatchRegistry {
  public:
    ResonanceProbeBatchRegistry() = default;

    /// Load probe batch. Returns handle or -1. Caller must validate compatibility before calling.
    int32_t load_batch(IPLContext ctx, IPLSimulator sim, std::mutex* sim_mutex,
                       Ref<ResonanceProbeData> data, uint64_t data_hash);

    void remove_batch(int32_t handle, IPLSimulator sim, std::mutex* sim_mutex);

    void clear_batches(IPLSimulator sim, std::mutex* sim_mutex);

    int revalidate_with_config(IPLSimulator sim, std::mutex* sim_mutex,
                               int reflection_type, bool pathing_enabled);

    /// Returns pathing batch for preferred_handle if valid, else first with pathing. Caller must iplProbeBatchRelease.
    IPLProbeBatch get_pathing_batch(int32_t preferred_handle) const;

    bool is_compatible(int32_t handle, int reflection_type, bool pathing_enabled) const;

    ProbeBatchManager& get_manager() { return probe_batch_manager_; }
    const ProbeBatchManager& get_manager() const { return probe_batch_manager_; }

    void get_all_batches_for_shutdown(std::vector<IPLProbeBatch>& out);

    bool has_any_batches() const;

  private:
    static bool is_reflection_type_compatible(int baked_type, int reflection_type);

    ProbeBatchManager probe_batch_manager_;
    std::unordered_map<uint64_t, int32_t> hash_to_handle_;
    std::unordered_map<int32_t, uint64_t> handle_to_hash_;
    std::unordered_map<int32_t, int> refcount_;
    std::unordered_map<int32_t, bool> handle_has_pathing_;
    std::unordered_map<int32_t, int> handle_baked_refl_;
    mutable std::mutex mutex_;
};

} // namespace godot

#endif // RESONANCE_PROBE_BATCH_REGISTRY_H

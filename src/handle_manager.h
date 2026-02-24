#ifndef HANDLE_MANAGER_H
#define HANDLE_MANAGER_H

#include <phonon.h>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <vector>
#include <cstdint>

namespace godot {

/// Shared release helpers for Steam Audio handles (forward declarations; phonon.h required)
inline void _handle_release_source(IPLSource* p) {
    if (p && *p) iplSourceRelease(p);
}
inline void _handle_release_batch(IPLProbeBatch* p) {
    if (p && *p) iplProbeBatchRelease(p);
}

/// Generic handle-based resource manager. Reduces duplication between SourceManager and ProbeBatchManager.
/// T: stored resource type (IPLSource, IPLProbeBatch).
/// ReleaseFunc: void (*)(T*) - called when releasing a stored resource.
template<typename T, void (*ReleaseFunc)(T*)>
class HandleManagerBase {
protected:
    int32_t next_handle = 0;
    std::priority_queue<int32_t, std::vector<int32_t>, std::greater<int32_t>> free_handles;
    std::unordered_map<int32_t, T> items;
    mutable std::mutex mutex;

    int32_t alloc_handle() {
        if (!free_handles.empty()) {
            int32_t h = free_handles.top();
            free_handles.pop();
            return h;
        }
        return next_handle++;
    }

    void recycle_handle(int32_t h) { free_handles.push(h); }

    void release_all() {
        for (auto& pair : items) {
            if (pair.second) ReleaseFunc(&pair.second);
        }
        items.clear();
    }

    void clear_and_reset(std::vector<T>& out) {
        out.clear();
        for (auto& pair : items) {
            out.push_back(pair.second);
        }
        items.clear();
        free_handles = std::priority_queue<int32_t, std::vector<int32_t>, std::greater<int32_t>>();
        next_handle = 0;
    }
};

class ProbeBatchManager : public HandleManagerBase<IPLProbeBatch, _handle_release_batch> {
public:
    ProbeBatchManager();
    ~ProbeBatchManager();
    int32_t add_batch(IPLProbeBatch batch);
    IPLProbeBatch take_batch(int32_t handle);
    void get_all_batches(std::vector<IPLProbeBatch>& out);
    IPLProbeBatch get_first_batch();
    IPLProbeBatch get_batch(int32_t handle) const;
};

} // namespace godot

#endif // HANDLE_MANAGER_H

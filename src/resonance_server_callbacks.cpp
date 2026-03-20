#include "resonance_server.h"
#include "resonance_utils.h"
#include <cmath>
#include <limits>
#include <mutex>

using namespace godot;

void IPLCALL ResonanceServer::_pathing_vis_callback(IPLVector3 from, IPLVector3 to, IPLbool occluded, void* userData) {
    if (ResonanceServer::is_shutting_down_flag.load(std::memory_order_acquire))
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
    if (ResonanceServer::is_shutting_down_flag.load(std::memory_order_acquire))
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
    if (ResonanceServer::is_shutting_down_flag.load(std::memory_order_acquire))
        return;
    ResonanceServer* server = static_cast<ResonanceServer*>(userData);
    if (!server)
        return;

    std::vector<IPLHit> hits(numRays);
    int bounce = server->ray_debug_bounce_index_.load(std::memory_order_relaxed);
    server->ray_trace_debug_context_.trace_batch(numRays, rays, minDistances, maxDistances, hits.data(), bounce);
    for (IPLint32 i = 0; i < numRays; i++) {
        occluded[i] = (hits[i].distance < std::numeric_limits<IPLfloat32>::infinity()) ? 1 : 0;
    }
}

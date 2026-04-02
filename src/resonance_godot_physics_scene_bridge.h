#ifndef RESONANCE_GODOT_PHYSICS_SCENE_BRIDGE_H
#define RESONANCE_GODOT_PHYSICS_SCENE_BRIDGE_H

#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <phonon.h>

namespace godot {

/// Holds Godot world + query settings for Steam Audio IPL_SCENETYPE_CUSTOM callbacks.
class ResonanceGodotPhysicsSceneBridge {
  public:
    void set_world(const Ref<World3D>& world);
    void clear_world();
    bool has_valid_world() const { return world_.is_valid(); }

    void set_collision_mask(uint32_t mask);
    uint32_t get_collision_mask() const { return collision_mask_; }

    void set_exclude_rids(const TypedArray<RID>& exclude);
    const TypedArray<RID>& get_exclude_rids() const { return exclude_rids_; }

    void* user_data() { return static_cast<void*>(this); }

    static void IPLCALL closest_hit_callback(const IPLRay* ray, IPLfloat32 min_distance, IPLfloat32 max_distance, IPLHit* hit,
                                             void* user_data);
    static void IPLCALL any_hit_callback(const IPLRay* ray, IPLfloat32 min_distance, IPLfloat32 max_distance, IPLuint8* occluded,
                                         void* user_data);

  private:
    void trace_closest(const IPLRay& ray, float min_distance, float max_distance, IPLHit* out_hit);
    bool trace_any(const IPLRay& ray, float min_distance, float max_distance);

    Ref<World3D> world_;
    uint32_t collision_mask_ = 0xFFFFFFFFu;
    TypedArray<RID> exclude_rids_;
};

} // namespace godot

#endif

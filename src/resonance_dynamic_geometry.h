#ifndef RESONANCE_DYNAMIC_GEOMETRY_H
#define RESONANCE_DYNAMIC_GEOMETRY_H

#include "resonance_geometry.h"

namespace godot {

/// Dynamic geometry: excluded from static bake; mesh lives in a local sub-scene and is placed in the global IPLScene
/// as an instanced mesh (same idea as Steam Audio Unity Dynamic Object — rigid subtree, root transform updated at runtime).
/// Path validation / alternate paths need this transform plus iplSceneCommit before simulation (see ResonanceGeometry::_update_dynamic_transform).
/// Use for doors, moving platforms, and other animated objects. Holds mesh_asset; export via Tools menu.
class ResonanceDynamicGeometry : public ResonanceGeometry {
    GDCLASS(ResonanceDynamicGeometry, ResonanceGeometry)

  protected:
    static void _bind_methods();
    void _validate_property(PropertyInfo& p_property) const override;

  public:
    ResonanceDynamicGeometry();
    ~ResonanceDynamicGeometry() = default;
};

} // namespace godot

#endif // RESONANCE_DYNAMIC_GEOMETRY_H

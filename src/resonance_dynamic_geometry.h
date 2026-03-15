#ifndef RESONANCE_DYNAMIC_GEOMETRY_H
#define RESONANCE_DYNAMIC_GEOMETRY_H

#include "resonance_geometry.h"

namespace godot {

/// Dynamic geometry: excluded from baked export, updates transform at runtime for realtime rays.
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

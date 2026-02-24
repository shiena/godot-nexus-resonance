#ifndef RESONANCE_STATIC_GEOMETRY_H
#define RESONANCE_STATIC_GEOMETRY_H

#include "resonance_geometry.h"

namespace godot {

/// Static geometry: included in baked scene export, uses ResonanceStaticScene asset at runtime.
/// Use for walls, floors, and other non-moving objects. No dynamic parameter (always static).
class ResonanceStaticGeometry : public ResonanceGeometry {
    GDCLASS(ResonanceStaticGeometry, ResonanceGeometry)

protected:
    static void _bind_methods();
    void _validate_property(PropertyInfo& p_property) const override;

public:
    ResonanceStaticGeometry();
    ~ResonanceStaticGeometry();
};

} // namespace godot

#endif // RESONANCE_STATIC_GEOMETRY_H

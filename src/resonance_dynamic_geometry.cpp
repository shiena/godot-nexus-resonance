#include "resonance_dynamic_geometry.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string_name.hpp>

using namespace godot;

ResonanceDynamicGeometry::ResonanceDynamicGeometry() {
    set_dynamic(true);
}

ResonanceDynamicGeometry::~ResonanceDynamicGeometry() {}

void ResonanceDynamicGeometry::_bind_methods() {}

void ResonanceDynamicGeometry::_validate_property(PropertyInfo& p_property) const {
    if (p_property.name == StringName("dynamic")) {
        p_property.usage = PROPERTY_USAGE_NO_EDITOR;
    }
    ResonanceGeometry::_validate_property(p_property);
}

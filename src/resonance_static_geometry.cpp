#include "resonance_static_geometry.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string_name.hpp>

using namespace godot;

ResonanceStaticGeometry::ResonanceStaticGeometry() {
    set_dynamic(false);
}

ResonanceStaticGeometry::~ResonanceStaticGeometry() {}

void ResonanceStaticGeometry::_bind_methods() {}

void ResonanceStaticGeometry::_validate_property(PropertyInfo& p_property) const {
    if (p_property.name == StringName("dynamic") || p_property.name == StringName("mesh_asset")) {
        p_property.usage = PROPERTY_USAGE_NO_EDITOR;
    }
    ResonanceGeometry::_validate_property(p_property);
}

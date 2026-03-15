#include "resonance_debug_agent.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace godot {

/// Forward log to ResonanceLogger (GDScript) when available. Category for thematic filtering.
void resonance_logger_log(const char* category, const char* message, Dictionary data) {
    if (!category || !message)
        return;
    Engine* eng = Engine::get_singleton();
    if (!eng || !eng->has_singleton("ResonanceLogger"))
        return;
    Variant logger_var = eng->get_singleton("ResonanceLogger");
    if (logger_var.get_type() != Variant::OBJECT)
        return;
    Object* logger_obj = logger_var.operator Object*();
    if (!logger_obj || !logger_obj->has_method("log"))
        return;
    Array args;
    args.push_back(StringName(category));
    args.push_back(String(message));
    args.push_back(data);
    logger_obj->callv("log", args);
}

} // namespace godot

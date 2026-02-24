#ifndef RESONANCE_DEBUG_AGENT_H
#define RESONANCE_DEBUG_AGENT_H

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

/// Forward to ResonanceLogger (GDScript) when available. Use for thematic logging from C++.
void resonance_logger_log(const char* category, const char* message, Dictionary data);

} // namespace godot

#endif

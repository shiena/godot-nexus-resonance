#ifndef REGISTER_TYPES_H
#define REGISTER_TYPES_H

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

// Forward declaration of module initialization and uninitialization functions.
void initialize_nexus_resonance_module(ModuleInitializationLevel p_level);
void uninitialize_nexus_resonance_module(ModuleInitializationLevel p_level);

#endif // REGISTER_TYPES_H
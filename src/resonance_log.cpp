#include "resonance_log.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <iostream>

namespace godot {

ResonanceLog::LogLevel ResonanceLog::current_level = ResonanceLog::LEVEL_WARN; // LEVEL_INFO for more verbosity; LEVEL_TRACE only when debugging

void ResonanceLog::set_level(LogLevel p_level) {
    current_level = p_level;
}

void ResonanceLog::info(const String& p_msg) {
    if (current_level >= LEVEL_INFO) {
        // Godot Console
        UtilityFunctions::print("Nexus Resonance: ", p_msg);
        // System Console (Backup)
        std::cout << "Nexus Resonance: " << p_msg.utf8().get_data() << std::endl;
    }
}

void ResonanceLog::warn(const String& p_msg) {
    if (current_level >= LEVEL_WARN) {
        String full_msg = "Nexus Resonance: " + p_msg;
        UtilityFunctions::push_warning(full_msg);
        resonance_logger_log("warn", full_msg.utf8().get_data(), Dictionary());
        std::cout << "Nexus Resonance: " << p_msg.utf8().get_data() << std::endl;
    }
}

void ResonanceLog::error(const String& p_msg) {
    if (current_level >= LEVEL_ERROR) {
        String full_msg = "Nexus Resonance: " + p_msg;
        UtilityFunctions::push_error(full_msg);
        resonance_logger_log("error", full_msg.utf8().get_data(), Dictionary());
        std::cerr << "Nexus Resonance: " << p_msg.utf8().get_data() << std::endl;
    }
}

void ResonanceLog::trace(const String& p_msg) {
    if (current_level >= LEVEL_TRACE) {
        // We write directly to stdout and FLUSH to ensure it appears before a crash
        std::cout << "Nexus Resonance: " << p_msg.utf8().get_data() << std::endl;
        std::flush(std::cout);
    }
}

void ResonanceLog::check_ptr(const char* name, void* ptr) {
    if (current_level >= LEVEL_TRACE) {
        String status = (ptr == nullptr) ? "NULL" : "VALID";
        String msg = String("PTR CHECK: ") + String(name) + " is " + status;

        std::cout << "Nexus Resonance: " << msg.utf8().get_data() << std::endl;

        if (ptr == nullptr) {
            // If we are tracing and find a null, we force an error log too
            UtilityFunctions::push_error("Nexus Resonance: " + String(name) + " is NULL!");
        }
    }
}

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
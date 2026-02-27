#include "resonance_log.h"
#include "resonance_debug_agent.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstdio>

namespace godot {

    ResonanceLog::LogLevel ResonanceLog::current_level = ResonanceLog::LEVEL_WARN;  // LEVEL_INFO for more verbosity; LEVEL_TRACE only when debugging

    void ResonanceLog::set_level(LogLevel p_level) {
        current_level = p_level;
    }

    void ResonanceLog::info(const String& p_msg) {
        if (current_level >= LEVEL_INFO) {
            // Godot Console
            UtilityFunctions::print("[Nexus] INFO: ", p_msg);
            // System Console (Backup)
            std::cout << "[Nexus] INFO: " << p_msg.utf8().get_data() << std::endl;
        }
    }

    void ResonanceLog::warn(const String& p_msg) {
        if (current_level >= LEVEL_WARN) {
            String full_msg = "[Nexus] WARN: " + p_msg;
            UtilityFunctions::push_warning(full_msg);
            resonance_logger_log("warn", full_msg.utf8().get_data(), Dictionary());
            std::cout << "[Nexus] WARN: " << p_msg.utf8().get_data() << std::endl;
        }
    }

    void ResonanceLog::error(const String& p_msg) {
        if (current_level >= LEVEL_ERROR) {
            String full_msg = "[Nexus] ERROR: " + p_msg;
            UtilityFunctions::push_error(full_msg);
            resonance_logger_log("error", full_msg.utf8().get_data(), Dictionary());
            std::cerr << "[Nexus] ERROR: " << p_msg.utf8().get_data() << std::endl;
        }
    }

    void ResonanceLog::trace(const String& p_msg) {
        if (current_level >= LEVEL_TRACE) {
            // We write directly to stdout and FLUSH to ensure it appears before a crash
            std::cout << "[Nexus] TRACE: " << p_msg.utf8().get_data() << std::endl;
            std::flush(std::cout);
        }
    }

    void ResonanceLog::check_ptr(const char* name, void* ptr) {
        if (current_level >= LEVEL_TRACE) {
            String status = (ptr == nullptr) ? "NULL" : "VALID";
            String msg = String("PTR CHECK: ") + String(name) + " is " + status;

            std::cout << "[Nexus] " << msg.utf8().get_data() << std::endl;

            if (ptr == nullptr) {
                // If we are tracing and find a null, we force an error log too
                UtilityFunctions::push_error("[Nexus] CRITICAL: " + String(name) + " is NULL!");
            }
        }
    }

} // namespace godot
#pragma once
// Thread-safe C++-only debug logger. Safe to call from Steam Audio worker thread.
// Set path from main thread before starting simulation.

#include <fstream>
#include <chrono>
#include <string>
#include <mutex>

namespace resonance {
inline std::string g_debug_log_path = "debug-e92d3b.log";
inline std::mutex g_debug_log_mutex;

inline void set_debug_log_path(const char* path) {
    g_debug_log_path = (path && path[0]) ? path : "debug-e92d3b.log";
}

inline void debug_log_raw(const char* loc, const char* msg, int v1, int v2) {
    std::lock_guard<std::mutex> lock(g_debug_log_mutex);
    std::ofstream f(g_debug_log_path, std::ios::app);
    if (!f) return;
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    f << "{\"sessionId\":\"e92d3b\",\"location\":\"" << loc << "\",\"message\":\"" << msg << "\"";
    if (v1 >= 0) f << ",\"v1\":" << v1;
    if (v2 >= 0) f << ",\"v2\":" << v2;
    f << ",\"timestamp\":" << ts << "}\n";
    f.flush();
}
}

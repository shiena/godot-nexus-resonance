#ifndef RESONANCE_FMOD_BRIDGE_H
#define RESONANCE_FMOD_BRIDGE_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <phonon.h>

namespace godot {

/// Optional bridge between Nexus Resonance (Steam Audio) and FMOD Studio.
/// Dynamically loads the Steam Audio FMOD plugin (phonon_fmod.dll/so/dylib)
/// and calls iplFMODInitialize, iplFMODSetHRTF, etc. with ResonanceServer's state.
/// No FMOD SDK dependency when disabled; fails gracefully if plugin not found.
class ResonanceFMODBridge : public Object {
    GDCLASS(ResonanceFMODBridge, Object)

  public:
    ResonanceFMODBridge() = default;
    ~ResonanceFMODBridge();

    ResonanceFMODBridge(const ResonanceFMODBridge&) = delete;
    ResonanceFMODBridge(ResonanceFMODBridge&&) = delete;

    /// Initialize the FMOD bridge: load plugin DLL and call iplFMODInitialize etc.
    /// Requires ResonanceServer to be initialized. Returns true on success.
    bool init_bridge();

    /// Shutdown: call iplFMODTerminate and unload plugin.
    void shutdown_bridge();

    /// Whether the bridge is currently initialized and the plugin is loaded.
    bool is_bridge_loaded() const { return plugin_handle_ != nullptr; }

    /// Add an IPLSource for an FMOD 3D event.
    /// Returns handle (>= 0) for IPL_SPATIALIZE_SIMULATION_OUTPUTS_HANDLE, or -1 on failure.
    /// Use ResonanceServer.create_source_handle for the source, then pass via add_fmod_source.
    int32_t add_fmod_source(int32_t resonance_source_handle);

    /// Remove an FMOD source. Call when the FMOD event stops.
    void remove_fmod_source(int32_t fmod_handle);

  private:
#if defined(_WIN32) || defined(_WIN64)
    void* plugin_handle_ = nullptr; // HMODULE
#else
    void* plugin_handle_ = nullptr; // void* from dlopen
#endif

    bool initialized_ = false;

    // Function pointers (loaded from plugin)
    void (*fn_iplFMODInitialize_)(IPLContext) = nullptr;
    void (*fn_iplFMODTerminate_)() = nullptr;
    void (*fn_iplFMODSetHRTF_)(IPLHRTF) = nullptr;
    void (*fn_iplFMODSetSimulationSettings_)(IPLSimulationSettings) = nullptr;
    void (*fn_iplFMODSetReverbSource_)(IPLSource) = nullptr;
    int32_t (*fn_iplFMODAddSource_)(IPLSource) = nullptr;
    void (*fn_iplFMODRemoveSource_)(int32_t) = nullptr;

    bool load_plugin();
    void unload_plugin();
    void* get_proc(const char* name);

    static void _bind_methods();
};

} // namespace godot

#endif // RESONANCE_FMOD_BRIDGE_H

#include "resonance_constants.h"
#include "resonance_geometry_asset.h"
#include "resonance_server.h"
#include "resonance_utils.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void ResonanceServer::notify_geometry_changed(int triangle_delta) {
    if (!_ctx())
        return;
    bool should_mark_dirty = (triangle_delta != 0) || _should_run_throttled(geometry_update_throttle_counter, geometry_update_throttle);
    std::lock_guard<std::mutex> lock(simulation_mutex);
    global_triangle_count += triangle_delta;
    if (should_mark_dirty)
        scene_dirty = true;
}

void ResonanceServer::save_scene_data(String filename) {
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.save_scene_data(_ctx(), scene, filename);
}

void ResonanceServer::save_scene_obj(String file_base_name) {
    std::lock_guard<std::mutex> lock(simulation_mutex);
    if (!scene) {
        UtilityFunctions::push_warning("Nexus Resonance: No scene to export (save_scene_obj).");
        return;
    }
    String abs_path = file_base_name;
    if (file_base_name.begins_with("res://") || file_base_name.begins_with("user://")) {
        ProjectSettings* ps = ProjectSettings::get_singleton();
        if (ps)
            abs_path = ps->globalize_path(file_base_name);
    }
    // Steam Audio dumpObj writes the OBJ file to the given path as-is (no .obj appended).
    if (!abs_path.ends_with(".obj")) {
        abs_path = abs_path + ".obj";
    }
    CharString path = abs_path.utf8();
    iplSceneSaveOBJ(scene, path.get_data());
    Engine* eng = Engine::get_singleton();
    if (eng && eng->is_editor_hint()) {
        UtilityFunctions::print_rich("[color=cyan]Nexus Resonance:[/color] Scene exported to OBJ (base: " + file_base_name + ").");
    }
}

void ResonanceServer::load_scene_data(String filename) {
    if (!_ctx() || !simulator)
        return;
    std::lock_guard<std::mutex> lock(simulation_mutex);
    scene_manager_.load_scene_data(_ctx(), &scene, simulator, _scene_type(), _embree(), _radeon(), filename, &global_triangle_count);
}

Error ResonanceServer::export_static_scene_to_asset(Node* scene_root, const String& p_path) {
    return scene_manager_.export_static_scene_to_asset(scene_root, p_path);
}

Error ResonanceServer::export_static_scene_to_obj(Node* scene_root, const String& file_base_name) {
    return scene_manager_.export_static_scene_to_obj(scene_root, file_base_name);
}

int64_t ResonanceServer::get_static_scene_hash(Node* scene_root) {
    return scene_manager_.get_static_scene_hash(scene_root, [this](const PackedByteArray& pba) { return _hash_probe_data(pba); });
}

int64_t ResonanceServer::get_geometry_asset_hash(const Ref<ResonanceGeometryAsset>& p_asset) const {
    if (!p_asset.is_valid() || !p_asset->is_valid())
        return 0;
    PackedByteArray data = p_asset->get_mesh_data();
    return static_cast<int64_t>(_hash_probe_data(data));
}
uint64_t ResonanceServer::_hash_probe_data(const PackedByteArray& pba) {
    const uint8_t* ptr = pba.ptr();
    return resonance::fnv1a_hash(ptr, static_cast<size_t>(pba.size()));
}

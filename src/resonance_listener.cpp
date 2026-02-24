#include "resonance_listener.h"
#include "resonance_server.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

ResonanceListener::ResonanceListener() {
}

ResonanceListener::~ResonanceListener() {
    if (reflection_mesh_instance) {
        reflection_mesh_instance->queue_free();
        reflection_mesh_instance = nullptr;
    }
}

void ResonanceListener::_enter_tree() {
    add_to_group("resonance_listener");
}

void ResonanceListener::_exit_tree() {
    remove_from_group("resonance_listener");
}

void ResonanceListener::_process(double delta) {
    Engine* eng = Engine::get_singleton();
    if (eng && eng->is_editor_hint()) return;

    ResonanceServer* server = ResonanceServer::get_singleton();
    if (!server || !server->is_initialized()) return;

    server->set_listener_valid(listener_valid);

    Transform3D gt = get_global_transform();
    Vector3 position = gt.origin;
    Vector3 forward = -gt.basis.get_column(2);
    Vector3 up = gt.basis.get_column(1);

    server->update_listener(position, forward, up);

    if (server->wants_debug_reflection_viz()) {
        Array segments = server->get_ray_debug_segments();
        if (!segments.is_empty()) {
            _ensure_reflection_viz();
            _draw_reflection_rays(segments);
            if (reflection_mesh_instance) reflection_mesh_instance->set_visible(true);
        } else {
            if (reflection_mesh_instance) reflection_mesh_instance->set_visible(false);
        }
    } else {
        if (reflection_mesh_instance) reflection_mesh_instance->set_visible(false);
    }

    // Heartbeat Debug (1x per sec)
    heartbeat_timer += delta;
    if (heartbeat_timer > 1.0) {
        heartbeat_timer = 0.0;
        // UtilityFunctions::print("Nexus Resonance: Listener Tick Active at ", position);
    }
}

void ResonanceListener::_ensure_reflection_viz() {
    if (reflection_mesh_instance) return;

    reflection_material.instantiate();
    reflection_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    reflection_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    reflection_material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);

    reflection_immediate_mesh.instantiate();
    reflection_mesh_instance = memnew(MeshInstance3D);
    reflection_mesh_instance->set_mesh(reflection_immediate_mesh);
    reflection_mesh_instance->set_material_override(reflection_material);
    reflection_mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    reflection_mesh_instance->set_name("ResonanceReflectionRayDebugViz");
    add_child(reflection_mesh_instance);
}

void ResonanceListener::_draw_reflection_rays(const Array& segments) {
    if (!reflection_immediate_mesh.is_valid()) return;

    reflection_immediate_mesh->clear_surfaces();
    reflection_immediate_mesh->surface_begin(Mesh::PRIMITIVE_LINES);

    const Color col = Color(0.2f, 1.0f, 0.2f);
    reflection_immediate_mesh->surface_set_color(col);

    for (int i = 0; i < segments.size(); i++) {
        Variant v = segments[i];
        if (v.get_type() != Variant::DICTIONARY) continue;
        Dictionary d = v;
        Variant from_v = d.get("from", Vector3());
        Variant to_v = d.get("to", Vector3());
        Vector3 from_pt = from_v;
        Vector3 to_pt = to_v;
        reflection_immediate_mesh->surface_add_vertex(to_local(from_pt));
        reflection_immediate_mesh->surface_add_vertex(to_local(to_pt));
    }
    reflection_immediate_mesh->surface_end();
}

void ResonanceListener::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_listener_valid", "valid"), &ResonanceListener::set_listener_valid);
    ClassDB::bind_method(D_METHOD("is_listener_valid"), &ResonanceListener::is_listener_valid);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "listener_valid"), "set_listener_valid", "is_listener_valid");
}
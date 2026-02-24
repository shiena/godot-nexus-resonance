#include "resonance_debug_drawer.h"
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

ResonanceDebugDrawer::ResonanceDebugDrawer() {
    material.instantiate();
    material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
}

ResonanceDebugDrawer::~ResonanceDebugDrawer() {
    cleanup();
}

void ResonanceDebugDrawer::initialize(Node3D* p_parent) {
    parent_node = p_parent;
}

void ResonanceDebugDrawer::cleanup() {
    if (mesh_instance) {
        mesh_instance->queue_free();
        mesh_instance = nullptr;
    }
    if (label_instance) {
        label_instance->queue_free();
        label_instance = nullptr;
    }
    // immediate_mesh is freed by mesh_instance or ref counting
    parent_node = nullptr;
}

void ResonanceDebugDrawer::_create_visuals_if_needed() {
    if (!parent_node) return;

    if (!mesh_instance) {
        mesh_instance = memnew(MeshInstance3D);
        immediate_mesh = memnew(ImmediateMesh);
        mesh_instance->set_mesh(immediate_mesh);
        mesh_instance->set_material_override(material);
        mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        parent_node->add_child(mesh_instance);
    }

    if (!label_instance) {
        label_instance = memnew(Label3D);
        label_instance->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
        label_instance->set_position(Vector3(0, 0.5, 0)); // Offset above player
        label_instance->set_pixel_size(0.005f);
        label_instance->set_modulate(Color(1, 1, 0)); // Yellow
        label_instance->set("no_depth_test", true);   // See through walls
        label_instance->set_visible(false);
        parent_node->add_child(label_instance);
    }
}

void ResonanceDebugDrawer::_draw_line(const Vector3& from, const Vector3& to, float occlusion) {
    if (!immediate_mesh || !parent_node) return;

    immediate_mesh->clear_surfaces();
    immediate_mesh->surface_begin(Mesh::PRIMITIVE_LINES);

    // Color gradient based on occlusion (Red = Blocked, Green = Open)
    Color col = Color(1.0f - occlusion, 1.0f - (occlusion * 0.5f), 0.0f);
    immediate_mesh->surface_set_color(col);

    // Convert global positions to local space of the player/drawer parent
    // "from" is usually the source position (0,0,0 local)
    // "to" is the listener position
    Vector3 p1 = parent_node->to_local(from);
    Vector3 p2 = parent_node->to_local(to);

    immediate_mesh->surface_add_vertex(p1);
    immediate_mesh->surface_add_vertex(p2);

    immediate_mesh->surface_end();
}

void ResonanceDebugDrawer::_update_label_text(const ResonanceDebugData& data, String node_name) {
    if (!label_instance) return;

    String air_str = data.air_abs_enabled
        ? ("Air L/M/H: " + String::num(data.air_absorption.x, 2) + " / " + String::num(data.air_absorption.y, 2) + " / " + String::num(data.air_absorption.z, 2))
        : "Air: OFF";

    String dir_str = data.directivity_enabled
        ? ("Dir: " + String::num(data.directivity_val, 2))
        : "";

    String text = "";
    text += "Occ: " + String::num(data.occlusion, 2) + "\n";
    text += "Trans L/M/H: " + String::num(data.transmission[0], 2) + " / " + String::num(data.transmission[1], 2) + " / " + String::num(data.transmission[2], 2) + "\n";
    text += "Atten: " + String::num(data.attenuation, 2) + "\n";
    text += air_str + "\n";
    if (data.directivity_enabled) text += dir_str + "\n";

    text += "Reverb: " + String(data.has_reverb ? "Active" : "None");

    label_instance->set_text(text);
}

void ResonanceDebugDrawer::process(double delta, const ResonanceDebugData& data, bool show_occ, bool show_reverb, String node_name) {
    // 1. Cleanup check
    if (!show_occ && !show_reverb) {
        if (mesh_instance && mesh_instance->is_visible()) mesh_instance->set_visible(false);
        if (label_instance && label_instance->is_visible()) label_instance->set_visible(false);
        return;
    }

    _create_visuals_if_needed();

    // 2. Lines from source to listener: not drawn for "debug sources".
    //    debug_sources shows only source-local info (label), not listener-facing rays.
    if (mesh_instance) mesh_instance->set_visible(false);

    // 3. Update Label (Throttled for readability/performance)
    update_timer += delta;
    if (update_timer >= LABEL_UPDATE_RATE) {
        update_timer = 0.0;
        if (show_occ || show_reverb) {
            _update_label_text(data, node_name);
            label_instance->set_visible(true);
        }
        else {
            label_instance->set_visible(false);
        }
    }
}
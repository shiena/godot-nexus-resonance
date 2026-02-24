#ifndef RESONANCE_LISTENER_H
#define RESONANCE_LISTENER_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/array.hpp>

namespace godot {

    class ResonanceListener : public Node3D {
        GDCLASS(ResonanceListener, Node3D)

    public:
        ResonanceListener();
        ~ResonanceListener();

        void set_listener_valid(bool valid) { listener_valid = valid; }
        bool is_listener_valid() const { return listener_valid; }

        void _enter_tree() override;
        void _exit_tree() override;
        void _process(double delta) override;

    protected:
        static void _bind_methods();

        bool listener_valid = true;
        double heartbeat_timer = 0.0;

        void _ensure_reflection_viz();
        void _draw_reflection_rays(const Array& segments);

        MeshInstance3D* reflection_mesh_instance = nullptr;
        Ref<ImmediateMesh> reflection_immediate_mesh;
        Ref<StandardMaterial3D> reflection_material;
    };

} // namespace godot

#endif // RESONANCE_LISTENER_H
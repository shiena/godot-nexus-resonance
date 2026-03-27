#ifndef RESONANCE_RUNTIME_NODE_H
#define RESONANCE_RUNTIME_NODE_H

#include <godot_cpp/classes/node.hpp>

namespace godot {

/// Native Node subclass for ClassDB and editor help; logic is in resonance_runtime.gd (custom type).
class ResonanceRuntime : public Node {
    GDCLASS(ResonanceRuntime, Node)

protected:
    static void _bind_methods();

public:
    ResonanceRuntime() = default;
};

} // namespace godot

#endif

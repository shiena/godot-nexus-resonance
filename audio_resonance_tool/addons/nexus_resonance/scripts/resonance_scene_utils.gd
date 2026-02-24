extends RefCounted
class_name ResonanceSceneUtils

## Shared scene traversal utilities for Nexus Resonance.
## Centralizes _find_resonance_static_scene, _collect_resonance_probe_volumes, _collect_resonance_dynamic_geometry.

## Finds first ResonanceStaticScene in node tree.
static func find_resonance_static_scene(node: Node) -> Node:
	if not node:
		return null
	if node.is_class("ResonanceStaticScene"):
		return node
	for c in node.get_children():
		var found = find_resonance_static_scene(c)
		if found:
			return found
	return null


## Collects all ResonanceProbeVolume nodes in node tree.
static func collect_resonance_probe_volumes(node: Node, collected: Array) -> void:
	if not node:
		return
	if node.is_class("ResonanceProbeVolume"):
		collected.append(node)
	for c in node.get_children():
		collect_resonance_probe_volumes(c, collected)


## Collects all ResonanceDynamicGeometry nodes in node tree.
static func collect_resonance_dynamic_geometry(node: Node, collected: Array) -> void:
	if not node:
		return
	if node.is_class("ResonanceDynamicGeometry"):
		collected.append(node)
	for c in node.get_children():
		collect_resonance_dynamic_geometry(c, collected)


## Collects all ResonanceStaticScene nodes in node tree.
static func collect_resonance_static_scenes(node: Node, collected: Array) -> void:
	if not node:
		return
	if node.is_class("ResonanceStaticScene"):
		collected.append(node)
	for c in node.get_children():
		collect_resonance_static_scenes(c, collected)

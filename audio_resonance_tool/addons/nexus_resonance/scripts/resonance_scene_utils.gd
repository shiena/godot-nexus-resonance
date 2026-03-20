@tool
extends RefCounted
class_name ResonanceSceneUtils

## Shared scene traversal utilities for Nexus Resonance.
## Centralizes _find_resonance_static_scene, _collect_resonance_probe_volumes, _collect_resonance_dynamic_geometry.


## Returns true if node or any descendant is ResonanceRuntime.
static func scene_has_resonance_runtime(node: Node) -> bool:
	if not node:
		return false
	if node.is_class("ResonanceRuntime"):
		return true
	for c in node.get_children():
		if scene_has_resonance_runtime(c):
			return true
	return false


## Returns true if scene has exportable resonance content for the given export type.
## Used to decide whether a scene should be included in static/dynamic export.
## export_type: "static" -> ResonanceRuntime OR ResonanceStaticGeometry OR ResonanceStaticScene with valid asset
## export_type: "dynamic" -> ResonanceDynamicGeometry
static func scene_has_exportable_resonance_content(node: Node, export_type: StringName) -> bool:
	if not node:
		return false
	match export_type:
		"static":
			if node.is_class("ResonanceRuntime"):
				return true
			if node.is_class("ResonanceStaticGeometry"):
				return true
			if node.is_class("ResonanceStaticScene"):
				if node.has_method("has_valid_asset") and node.has_valid_asset():
					return true
			for c in node.get_children():
				if scene_has_exportable_resonance_content(c, export_type):
					return true
			return false
		"dynamic":
			if node.is_class("ResonanceDynamicGeometry"):
				return true
			for c in node.get_children():
				if scene_has_exportable_resonance_content(c, export_type):
					return true
			return false
		_:
			return false


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
static func collect_resonance_probe_volumes(node: Node, collected: Array[Node]) -> void:
	if not node:
		return
	if node.is_class("ResonanceProbeVolume"):
		collected.append(node)
	for c in node.get_children():
		collect_resonance_probe_volumes(c, collected)


## Collects all ResonanceDynamicGeometry nodes in node tree.
static func collect_resonance_dynamic_geometry(node: Node, collected: Array[Node]) -> void:
	if not node:
		return
	if node.is_class("ResonanceDynamicGeometry"):
		collected.append(node)
	for c in node.get_children():
		collect_resonance_dynamic_geometry(c, collected)


## Collects all ResonanceStaticScene nodes in node tree.
static func collect_resonance_static_scenes(node: Node, collected: Array[Node]) -> void:
	if not node:
		return
	if node.is_class("ResonanceStaticScene"):
		collected.append(node)
	for c in node.get_children():
		collect_resonance_static_scenes(c, collected)

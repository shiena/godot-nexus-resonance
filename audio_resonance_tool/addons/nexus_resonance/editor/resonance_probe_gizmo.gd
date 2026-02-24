@tool
extends EditorNode3DGizmoPlugin

## Nexus Resonance Gizmo for ResonanceProbeVolume

const GIZMO_RAY_LENGTH := 10000.0
const MIN_REGION_SIZE := 0.1

var undo_redo: EditorUndoRedoManager

func _get_valid_region_size(node: Node) -> Vector3:
	var size = node.get("region_size")
	if size == null or typeof(size) != TYPE_VECTOR3:
		return Vector3.ONE
	return size

func _get_gizmo_name():
	return "ResonanceProbeVolume"

func _has_gizmo(node):
	return node.is_class("ResonanceProbeVolume")

func _init():
	# define materials
	create_material("main", Color(0.1, 0.8, 1.0))
	create_handle_material("handles")
	
	# load icon
	var icon_path = "res://addons/nexus_resonance/ui/icons/probe_volume_gizmo.svg"
	var icon_tex = null
	if FileAccess.file_exists(icon_path):
		icon_tex = load(icon_path)
	
	# Fallback to default icon
	if not icon_tex:
		var gui = EditorInterface.get_base_control()
		if gui:
			icon_tex = gui.get_theme_icon("ReflectionProbe", "EditorIcons")
			
	if icon_tex:
		create_icon_material("probe_icon", icon_tex)

func _redraw(gizmo: EditorNode3DGizmo):
	gizmo.clear()
	var node = gizmo.get_node_3d()
	
	# add icon
	var icon_mat = get_material("probe_icon", gizmo)
	if icon_mat:
		gizmo.add_unscaled_billboard(icon_mat, 0.05)
	
	var size = _get_valid_region_size(node)
	var lines = PackedVector3Array()
	var half = size * 0.5
	
	var p = [
		Vector3(-half.x, -half.y, -half.z), Vector3(half.x, -half.y, -half.z),
		Vector3(half.x, half.y, -half.z), Vector3(-half.x, half.y, -half.z),
		Vector3(-half.x, -half.y, half.z), Vector3(half.x, -half.y, half.z),
		Vector3(half.x, half.y, half.z), Vector3(-half.x, half.y, half.z)
	]
	
	lines.append_array([p[0], p[1], p[1], p[5], p[5], p[4], p[4], p[0]])
	lines.append_array([p[3], p[2], p[2], p[6], p[6], p[7], p[7], p[3]])
	lines.append_array([p[0], p[3], p[1], p[2], p[5], p[6], p[4], p[7]])
	
	gizmo.add_lines(lines, get_material("main", gizmo))
	gizmo.add_collision_segments(lines)
	
	# Handles
	var handles = PackedVector3Array()
	handles.push_back(Vector3(half.x, 0, 0))
	handles.push_back(Vector3(0, half.y, 0))
	handles.push_back(Vector3(0, 0, half.z))
	handles.push_back(Vector3(-half.x, 0, 0))
	handles.push_back(Vector3(0, -half.y, 0))
	handles.push_back(Vector3(0, 0, -half.z))
	
	gizmo.add_handles(handles, get_material("handles", gizmo), [])

func _get_handle_name(gizmo, handle_id, secondary):
	var names = ["Size +X", "Size +Y", "Size +Z", "Size -X", "Size -Y", "Size -Z"]
	return names[handle_id] if handle_id < names.size() else ""

func _get_handle_value(gizmo, handle_id, secondary):
	return _get_valid_region_size(gizmo.get_node_3d())

func _set_handle(gizmo, handle_id, secondary, camera, point):
	var node = gizmo.get_node_3d()
	var size = _get_valid_region_size(node)
	var size_mut = Vector3(size)
	
	var transform = node.global_transform
	var origin = transform.origin
	var basis = transform.basis
	
	var axis_dir = Vector3.ZERO
	if handle_id == 0: axis_dir = basis.x.normalized()
	elif handle_id == 1: axis_dir = basis.y.normalized()
	elif handle_id == 2: axis_dir = basis.z.normalized()
	elif handle_id == 3: axis_dir = -basis.x.normalized()
	elif handle_id == 4: axis_dir = -basis.y.normalized()
	elif handle_id == 5: axis_dir = -basis.z.normalized()
	
	var ray_from = camera.project_ray_origin(point)
	var ray_dir = camera.project_ray_normal(point)
	
	var axis_start = origin - axis_dir * GIZMO_RAY_LENGTH
	var axis_end = origin + axis_dir * GIZMO_RAY_LENGTH
	
	var points = Geometry3D.get_closest_points_between_segments(ray_from, ray_from + ray_dir * GIZMO_RAY_LENGTH, axis_start, axis_end)
	var closest_point = points[1]
	
	var dist = (closest_point - origin).dot(axis_dir)
	if Input.is_key_pressed(KEY_CTRL): dist = round(dist * 2.0) / 2.0
	
	var axis_idx = handle_id % 3
	var new_full_size = (dist * 2.0) if dist > 0 else MIN_REGION_SIZE
	if new_full_size < MIN_REGION_SIZE:
		new_full_size = MIN_REGION_SIZE
	
	size_mut[axis_idx] = new_full_size
	node.set("region_size", size_mut)
	node.update_gizmos()

func _commit_handle(gizmo, handle_id, secondary, restore, cancel):
	var node = gizmo.get_node_3d()
	if cancel:
		node.set("region_size", restore)
	else:
		var new_size = node.get("region_size")
		if undo_redo != null and new_size != restore:
			undo_redo.create_action("Change Resonance Probe Volume Size")
			undo_redo.add_do_method(node, "set", "region_size", new_size)
			undo_redo.add_undo_method(node, "set", "region_size", restore)
			undo_redo.add_do_method(node, "update_gizmos")
			undo_redo.add_undo_method(node, "update_gizmos")
			undo_redo.commit_action()
			return
	node.update_gizmos()
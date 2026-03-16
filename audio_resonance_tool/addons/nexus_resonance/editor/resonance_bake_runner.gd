@tool
extends RefCounted
class_name ResonanceBakeRunner

## Shared bake logic for ResonanceProbeVolume. Used by inspector plugin and Project menu.
## Run bake for selected volume(s) or all volumes in scene. Hash-based incremental baking.

const ResonanceRuntimeConfig = preload("res://addons/nexus_resonance/scripts/resonance_runtime_config.gd")
const ResonanceBakeConfig = preload("res://addons/nexus_resonance/scripts/resonance_bake_config.gd")
const ResonanceRuntimeScript = preload("res://addons/nexus_resonance/scripts/resonance_runtime.gd")
const ResonancePaths = preload("res://addons/nexus_resonance/scripts/resonance_paths.gd")
const ResonanceSceneUtils = preload("res://addons/nexus_resonance/scripts/resonance_scene_utils.gd")
const ResonanceEditorDialogs = preload("res://addons/nexus_resonance/editor/resonance_editor_dialogs.gd")
const ResonanceBakeProgressUI = preload("res://addons/nexus_resonance/editor/resonance_bake_progress_ui.gd")
const ResonanceBakeBackup = preload("res://addons/nexus_resonance/editor/resonance_bake_backup.gd")
const UIStrings = preload("res://addons/nexus_resonance/scripts/resonance_ui_strings.gd")

const BAKE_INITIAL_DELAY_SEC := 1.5
const BAKE_VOLUME_DELAY_SEC := 0.5
const DEFAULT_BAKE_INFLUENCE_RADIUS := 10000.0
const BAKE_RAY_BASE_SEC_PER_PROBE := 0.001
const BAKE_RAY_BASE_COUNT := 4096

class VolumeBakeContext:
	var root: Node
	var vol: Node
	var probe_data: Resource
	var need_reflections: bool
	var need_pathing: bool
	var need_static_source: bool
	var need_static_listener: bool
	var add_flags: Dictionary
	var player_pos: Vector3
	var player_radius: float
	var listener_pos: Vector3
	var listener_radius: float
	var bc: Resource
	var vol_info: String
	var static_asset = null  # ResonanceGeometryAsset used for bake; for static_scene_params_hash

var editor_interface: EditorInterface
## When set, used as quick link in validation dialog when static scene not exported.
var export_static_callback: Callable = Callable()
var _bake_in_progress: bool = false

var _progress_ui: ResonanceBakeProgressUI
var _backup: ResonanceBakeBackup

func _init(p_editor_interface: EditorInterface) -> void:
	editor_interface = p_editor_interface
	_progress_ui = ResonanceBakeProgressUI.new(p_editor_interface)
	_backup = ResonanceBakeBackup.new()

func run_bake(volumes: Array[Node]) -> void:
	if volumes.is_empty() or _bake_in_progress:
		return
	var root = _get_edited_scene_root(volumes)
	if not root:
		_log_and_show_error("No scene open", "Open a scene before baking.")
		return
	var static_scene_node = _find_resonance_static_scene_for_bake(volumes)
	var static_asset = static_scene_node.get("static_scene_asset") if static_scene_node else null
	_do_run_bake_with_backup(volumes, root, static_scene_node, static_asset)

## Returns validation checklist for inspector display. Array of { "label": str, "ok": bool }.
func get_validation_checklist_for_volume(vol: Node) -> Array:
	var vols: Array[Node] = []
	if vol:
		vols.append(vol)
	var root = _get_edited_scene_root(vols) if vol else null
	var static_scene_node = _find_resonance_static_scene_for_bake(vols) if vol else null
	var static_asset = static_scene_node.get("static_scene_asset") if static_scene_node else null
	return _build_validation_checklist(vols, root, static_scene_node, static_asset)

func _build_validation_checklist(volumes: Array[Node], root: Node, static_scene_node: Node, static_asset) -> Array:
	var gdext_ok = Engine.has_singleton("ResonanceServer")
	var static_exported = static_scene_node != null and static_scene_node.has_valid_asset()
	# has_valid_asset() already verifies the geometry asset; no separate GDScript check needed
	var has_geometry = static_exported
	var runtime_node = _find_resonance_runtime(root) if root else null
	var runtime_ok = runtime_node != null
	var audio_data_writable = _check_audio_data_writable()
	return [
		{"label": "GDExtension loaded", "ok": gdext_ok},
		{"label": "Static scene exported (Tools > Export Static Scene)", "ok": static_exported},
		{"label": "Geometry in static asset", "ok": has_geometry},
		{"label": "ResonanceRuntime in scene", "ok": runtime_ok},
		{"label": "audio_data/ writable", "ok": audio_data_writable},
		{"label": "%d Probe Volume(s) to bake" % volumes.size(), "ok": volumes.size() > 0},
	]

func _do_run_bake_with_backup(volumes: Array[Node], root: Node, static_scene_node: Node, static_asset) -> void:
	_backup.create_backups(volumes)
	_do_run_bake_after_validation(volumes, root, static_scene_node, static_asset, [])

func _do_run_bake_after_validation(volumes: Array[Node], root: Node, static_scene_node: Node, static_asset, _checklist: Array) -> void:
	if not static_scene_node or not static_scene_node.has_valid_asset():
		_log_and_show_error("Static scene not exported", "Use Tools > Nexus Resonance > Export Static Scene before baking.", "Scene must be exported first.")
		return
	if not Engine.has_singleton("ResonanceServer"):
		_log_and_show_error("GDExtension not loaded", "The ResonanceServer GDExtension is not available.")
		return
	if not _ensure_resonance_server_initialized(volumes):
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if srv and srv.has_method("set_bake_static_scene_asset"):
		srv.set_bake_static_scene_asset(static_asset)
	_bake_in_progress = true
	_progress_ui.show_ui()
	var base_ctrl = editor_interface.get_base_control() if editor_interface else null
	var tree: SceneTree = base_ctrl.get_tree() if base_ctrl else null
	if tree:
		var cb = func() -> void:
			_run_bake_pipeline_main_thread(volumes)
		tree.process_frame.connect(cb, CONNECT_ONE_SHOT)
	else:
		call_deferred("_run_bake_pipeline_main_thread", volumes)

func _get_edited_scene_root(volumes: Array[Node]) -> Node:
	if editor_interface:
		var root = editor_interface.get_edited_scene_root()
		if root:
			return root
	if volumes.size() > 0:
		var n: Node = volumes[0]
		while n.get_parent():
			n = n.get_parent()
		return n
	return null

## Estimates probe count for a volume. Uses region_size, spacing, generation_type.
func estimate_probe_count(vol: Node) -> int:
	if not vol or not vol.has_method("get") or not ("region_size" in vol and "spacing" in vol):
		return -1
	var extents: Vector3 = vol.get("region_size") * 0.5
	var spacing: float = vol.get("spacing")
	if spacing <= 0:
		return -1
	var gen_type: int = vol.get("generation_type") if "generation_type" in vol else 1
	# 0=Octree, 1=Floor (default), 2=...
	if gen_type == 1:
		return int(ceil(extents.x * 2 / spacing)) * int(ceil(extents.z * 2 / spacing))
	return int(ceil(extents.x * 2 / spacing)) * int(ceil(extents.y * 2 / spacing)) * int(ceil(extents.z * 2 / spacing))

## Rough bake time estimate in seconds. Based on probe count and bake config.
func estimate_bake_time(vol: Node) -> String:
	var count = estimate_probe_count(vol)
	if count < 0:
		return ""
	var bc = _get_bake_config_for_volume(vol)
	var rays = bc.bake_num_rays
	var bounces = bc.bake_num_bounces
	var threads = bc.bake_num_threads
	var pathing = bc.pathing_enabled
	var sec_per_probe = BAKE_RAY_BASE_SEC_PER_PROBE * (rays / float(BAKE_RAY_BASE_COUNT)) * bounces / max(1, threads)
	var total = count * sec_per_probe
	if pathing:
		total *= 2.0
	if total < 60:
		return "~%d s" % int(ceil(total))
	if total < 3600:
		return "~%d min" % int(ceil(total / 60))
	return "~%.1f h" % (total / 3600)

## Returns "Probes baked", "Outdated", or "Not baked" for inspector status display.
func get_volume_bake_status(vol: Node) -> String:
	if not vol or not vol.has_method("get_probe_data"):
		return "Not baked"
	var probe_data = vol.get_probe_data()
	if not probe_data:
		return "Not baked"
	var has_data = probe_data.get_data().size() > 0
	if not has_data:
		return "Not baked"
	var bc = _get_bake_config_for_volume(vol)
	var want_path = bc.pathing_enabled
	var path_hash = _compute_pathing_hash(bc) if want_path else 0
	var ph = probe_data.get_pathing_params_hash() if probe_data.has_method("get_pathing_params_hash") else 0
	var desired_refl = bc.reflection_type
	var refl_matches = probe_data.get_baked_reflection_type() == desired_refl if probe_data.has_method("get_baked_reflection_type") else false
	var hash_matches = probe_data.get_bake_params_hash() == vol.get_bake_params_hash() if vol.has_method("get_bake_params_hash") else false
	if not want_path and ph > 0:
		return "Outdated"
	if not hash_matches or not refl_matches:
		return "Outdated"
	if want_path and (ph == 0 or ph != path_hash):
		return "Outdated"
	var vols: Array[Node] = []
	vols.append(vol)
	var root = _get_edited_scene_root(vols)
	var static_scene_node = _find_resonance_static_scene_for_bake(vols)
	var static_asset = static_scene_node.get("static_scene_asset") if static_scene_node else null
	if static_asset and Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv.has_method("get_geometry_asset_hash") and probe_data.has_method("get_static_scene_params_hash"):
			var current_hash = srv.get_geometry_asset_hash(static_asset)
			var stored = probe_data.get_static_scene_params_hash()
			if current_hash != 0 and (stored == 0 or stored != current_hash):
				return "Outdated"
	if bc.static_source_enabled and root:
		var src = _resolve_bake_node_for_volume(vol, root, "bake_sources", "ResonancePlayer")
		if src and src is Node3D:
			var rad = vol.get("bake_influence_radius") if "bake_influence_radius" in vol else DEFAULT_BAKE_INFLUENCE_RADIUS
			var sh = _compute_position_radius_hash(src.global_position, rad)
			var ssh = probe_data.get_static_source_params_hash() if probe_data.has_method("get_static_source_params_hash") else 0
			if ssh == 0 or ssh != sh:
				return "Outdated"
	if bc.static_listener_enabled and root:
		var lst = _resolve_bake_node_for_volume(vol, root, "bake_listeners", "ResonanceListener")
		if lst and lst is Node3D:
			var rad = vol.get("bake_influence_radius") if "bake_influence_radius" in vol else DEFAULT_BAKE_INFLUENCE_RADIUS
			var lh = _compute_position_radius_hash(lst.global_position, rad)
			var lsh = probe_data.get_static_listener_params_hash() if probe_data.has_method("get_static_listener_params_hash") else 0
			if lsh == 0 or lsh != lh:
				return "Outdated"
	return "Probes baked"

## Ensures ResonanceServer is initialized and refreshes probe visuals.
## Call when a volume is selected in the inspector (so show_probes works without baking first).
func ensure_resonance_server_for_volumes(volumes: Array[Node]) -> bool:
	if not _ensure_resonance_server_initialized(volumes):
		return false
	_notify_volumes_viz_updated(volumes)
	return true

func _log_and_show_error(message: String, solution: String = "", cause: String = "", volume_name: String = "", step: String = "") -> void:
	var data := {"error": true}
	if not volume_name.is_empty():
		data["volume"] = volume_name
	if not step.is_empty():
		data["step"] = step
	if Engine.has_singleton("ResonanceLogger"):
		Engine.get_singleton("ResonanceLogger").log(&"bake", "Bake error: " + message, data)
	ResonanceEditorDialogs.show_error_dialog(
		editor_interface,
		tr(UIStrings.DIALOG_BAKE_FAILED_TITLE),
		message,
		cause,
		solution
	)

func _ensure_resonance_server_initialized(volumes: Array[Node]) -> bool:
	if not Engine.has_singleton("ResonanceServer"):
		_log_and_show_error("GDExtension not loaded.", "Ensure the Nexus Resonance GDExtension is installed.")
		return false
	var srv = Engine.get_singleton("ResonanceServer")
	if srv.is_initialized():
		return true
	var root = _get_edited_scene_root(volumes)
	var cfg_node = _find_resonance_runtime(root) if root else null
	var config := {}
	if cfg_node:
		if cfg_node.has_method("get_config_dict"):
			config = cfg_node.get_config_dict()
		else:
			var rt = cfg_node.get("runtime")
			if rt and rt.has_method("get_config"):
				config = rt.get_config()
				var src = cfg_node.get("debug_sources")
				config["debug_occlusion"] = false if src == null else src
	if config.is_empty():
		_log_and_show_error("No ResonanceRuntime in scene.", "Add a ResonanceRuntime node with valid ResonanceRuntimeConfig to the scene.")
		return false
	var bake_params := ResonanceBakeConfig.create_default().get_bake_params()
	if volumes.size() > 0:
		var bc = _get_bake_config_for_volume(volumes[0])
		if bc:
			bake_params = bc.get_bake_params()
	srv.set_bake_params(bake_params)
	srv.init_audio_engine(config)
	if not srv.is_initialized():
		_log_and_show_error("Server init failed.", "Check Editor output and ResonanceRuntime config. Ensure Steam Audio is properly configured.")
		return false
	return true

func _find_resonance_runtime(node: Node) -> Node:
	if not node:
		return null
	if node.get_script() == ResonanceRuntimeScript:
		return node
	if node.is_class("ResonanceRuntime"):
		return node
	for c in node.get_children():
		var found = _find_resonance_runtime(c)
		if found:
			return found
	return null

func _find_resonance_static_scene_for_bake(volumes: Array[Node]) -> Node:
	var root = _get_edited_scene_root(volumes)
	if root:
		var found = ResonanceSceneUtils.find_resonance_static_scene(root)
		if found:
			return found
	if volumes.size() > 0:
		var vol: Node = volumes[0]
		var branch_root: Node = vol
		while vol.get_parent():
			branch_root = vol.get_parent()
			vol = branch_root
		var found2 = ResonanceSceneUtils.find_resonance_static_scene(branch_root)
		if found2:
			return found2
	var tree: SceneTree = null
	if root:
		tree = root.get_tree()
	if not tree and volumes.size() > 0:
		tree = volumes[0].get_tree() if volumes[0].is_inside_tree() else null
	if tree:
		var scene_root = tree.get_edited_scene_root() if "edited_scene_root" in tree else tree.root
		if scene_root:
			return ResonanceSceneUtils.find_resonance_static_scene(scene_root)
	return null

func _get_bake_config_for_volume(vol) -> Resource:
	if vol.has_method("get_bake_config"):
		var bc = vol.get_bake_config()
		if bc:
			return bc
	return ResonanceBakeConfig.create_default()

func _get_additional_flags_for_volume(vol) -> Dictionary:
	var bc = _get_bake_config_for_volume(vol)
	return {"static_source": bc.static_source_enabled, "static_listener": bc.static_listener_enabled}

func _check_audio_data_writable() -> bool:
	var audio_dir: String = ResonancePaths.get_audio_data_dir()
	if not DirAccess.dir_exists_absolute(audio_dir):
		var err: int = DirAccess.make_dir_recursive_absolute(audio_dir)
		if err != OK:
			return false
	var test_path: String = audio_dir + ".write_test_" + str(Time.get_unix_time_from_system()) + ".tmp"
	var f = FileAccess.open(test_path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string("")
	f.close()
	DirAccess.remove_absolute(test_path)
	return true

func _hash_dict(d: Dictionary) -> int:
	return hash(var_to_str(d))

func _compute_pathing_hash(bc: Resource) -> int:
	var params = bc.get_bake_params()
	return _hash_dict({
		"vis_range": params.get("bake_pathing_vis_range", 500),
		"path_range": params.get("bake_pathing_path_range", 100),
		"num_samples": params.get("bake_pathing_num_samples", 16),
		"radius": params.get("bake_pathing_radius", 0.5),
		"threshold": params.get("bake_pathing_threshold", 0.1)
	})

func _compute_position_radius_hash(pos: Vector3, radius: float) -> int:
	return _hash_dict({"pos": pos, "radius": radius})

func _resolve_bake_node_for_volume(vol: Node, root: Node, property: String, target_class: String) -> Node:
	var arr = vol.get(property) if property in vol else []
	if arr is Array and arr.size() > 0:
		var path_val = arr[0]
		var path = NodePath(str(path_val)) if path_val else NodePath()
		if not path.is_empty():
			var n = vol.get_node_or_null(path) if vol.is_inside_tree() else null
			if not n and root:
				n = root.get_node_or_null(path)
			if n and n.is_class(target_class):
				return n
	return null

## Replicates the volume's probe_data path setup. Call on main thread before threaded bake.
func _prepare_probe_data_for_bake(vol: Node, probe_data: Resource, root: Node) -> void:
	if not probe_data or not vol or not root:
		return
	var scene_name := "unsaved"
	var scene_path = root.get_scene_file_path()
	if not scene_path.is_empty():
		scene_name = scene_path.get_file().get_basename()
	var node_name = str(vol.name).to_lower().replace(" ", "_")
	var audio_dir: String = ResonancePaths.get_audio_data_dir()
	var path: String = audio_dir + "%s_%s_baked_probes.tres" % [scene_name, node_name]
	if not DirAccess.dir_exists_absolute(audio_dir):
		var mkdir_err: int = DirAccess.make_dir_recursive_absolute(audio_dir)
		if mkdir_err != OK or not DirAccess.dir_exists_absolute(audio_dir):
			if Engine.has_singleton("ResonanceLogger"):
				Engine.get_singleton("ResonanceLogger").log(&"bake", "Failed to create audio output directory: %s" % mkdir_err, {"step": "prepare", "error": mkdir_err})
			return
	if probe_data.has_method("take_over_path"):
		probe_data.take_over_path(path)
	probe_data.emit_changed()
	if vol.has_method("get_bake_params_hash") and probe_data.has_method("set_bake_params_hash"):
		probe_data.set_bake_params_hash(vol.get_bake_params_hash())

## Runs a callable in a thread while the main thread polls for _cancel_requested.
## When cancel is pressed, calls srv.cancel_reflections_bake and cancel_pathing_bake.
## Returns the callable's return value.
func _run_in_thread_with_cancel_poll(bake_callable: Callable) -> Variant:
	var result: Variant = null
	var err: Error = OK
	var thread = Thread.new()
	thread.start(func() -> void:
		result = bake_callable.call()
	)
	var tree = editor_interface.get_base_control().get_tree() if editor_interface else null
	var srv = Engine.get_singleton("ResonanceServer") if Engine.has_singleton("ResonanceServer") else null
	while thread.is_alive():
		if tree:
			await tree.process_frame
		if _progress_ui.cancel_requested and srv:
			srv.cancel_reflections_bake()
			srv.cancel_pathing_bake()
	thread.wait_to_finish()
	return result

func _finish_pipeline(success: bool, probe_data_ref, volumes: Array[Node]) -> void:
	call_deferred("_on_bake_pipeline_finished", success, probe_data_ref, volumes)

func _wait_before_bake() -> bool:
	var tree = editor_interface.get_base_control().get_tree() if editor_interface else null
	if tree:
		await tree.process_frame
		await tree.create_timer(BAKE_INITIAL_DELAY_SEC).timeout
	return not _progress_ui.cancel_requested

func _build_volume_bake_context(vol: Node, root: Node, vol_index: int, total: int, static_asset = null) -> VolumeBakeContext:
	var ctx = VolumeBakeContext.new()
	ctx.root = root
	ctx.vol = vol
	ctx.static_asset = static_asset
	ctx.vol_info = " (volume %d of %d)" % [vol_index, total] if total > 1 else ""
	ctx.bc = _get_bake_config_for_volume(vol)
	ctx.add_flags = _get_additional_flags_for_volume(vol)
	ctx.player_pos = Vector3.ZERO
	ctx.player_radius = vol.get("bake_influence_radius") if "bake_influence_radius" in vol else DEFAULT_BAKE_INFLUENCE_RADIUS
	ctx.listener_pos = Vector3.ZERO
	ctx.listener_radius = vol.get("bake_influence_radius") if "bake_influence_radius" in vol else DEFAULT_BAKE_INFLUENCE_RADIUS
	if ctx.add_flags.static_source:
		var src = _resolve_bake_node_for_volume(vol, root, "bake_sources", "ResonancePlayer")
		if src and src is Node3D:
			ctx.player_pos = src.global_position
	if ctx.add_flags.static_listener:
		var lst = _resolve_bake_node_for_volume(vol, root, "bake_listeners", "ResonanceListener")
		if lst and lst is Node3D:
			ctx.listener_pos = lst.global_position
	var want_path = ctx.bc.pathing_enabled
	var path_hash = _compute_pathing_hash(ctx.bc) if want_path else 0
	var probe_data = vol.get_probe_data() if vol.has_method("get_probe_data") else null
	if not probe_data:
		probe_data = ClassDB.instantiate("ResonanceProbeData")
		vol.set_probe_data(probe_data)
	ctx.probe_data = probe_data
	var ph = probe_data.get_pathing_params_hash() if probe_data.has_method("get_pathing_params_hash") else 0
	var has_data = probe_data.get_data().size() > 0
	var desired_refl = ctx.bc.reflection_type
	var refl_matches = probe_data.get_baked_reflection_type() == desired_refl if probe_data.has_method("get_baked_reflection_type") else false
	var hash_matches = probe_data.get_bake_params_hash() == vol.get_bake_params_hash() if vol.has_method("get_bake_params_hash") else false
	ctx.need_reflections = not has_data or not hash_matches or not refl_matches
	# Static scene changed: geometry asset hash differs from what was used at last bake
	if static_asset and Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv.has_method("get_geometry_asset_hash") and probe_data.has_method("get_static_scene_params_hash"):
			var current_hash: int = srv.get_geometry_asset_hash(static_asset)
			var stored: int = probe_data.get_static_scene_params_hash()
			if stored == 0 or stored != current_hash:
				ctx.need_reflections = true
	if not want_path and ph > 0:
		ctx.need_reflections = true
	ctx.need_pathing = want_path and (ph == 0 or ph != path_hash)
	if want_path and ctx.need_pathing and (not has_data or not refl_matches):
		ctx.need_reflections = true
	ctx.need_static_source = false
	ctx.need_static_listener = false
	if ctx.add_flags.static_source:
		var sh = _compute_position_radius_hash(ctx.player_pos, ctx.player_radius)
		var ssh = probe_data.get_static_source_params_hash() if probe_data.has_method("get_static_source_params_hash") else 0
		ctx.need_static_source = ssh == 0 or ssh != sh
	if ctx.add_flags.static_listener:
		var lh = _compute_position_radius_hash(ctx.listener_pos, ctx.listener_radius)
		var lsh = probe_data.get_static_listener_params_hash() if probe_data.has_method("get_static_listener_params_hash") else 0
		ctx.need_static_listener = lsh == 0 or lsh != lh
	return ctx

func _run_bake_pipeline_main_thread(volumes: Array[Node]) -> void:
	_progress_ui.clear_details()
	_progress_ui.set_bake_status(tr(UIStrings.PROGRESS_PREPARING))
	_progress_ui.set_stage(0, volumes.size())
	if not await _wait_before_bake():
		return
	if not Engine.has_singleton("ResonanceServer"):
		_log_and_show_error("GDExtension unloaded", "ResonanceServer is no longer available. Bake aborted.")
		_finish_pipeline(false, null, volumes)
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if not srv:
		_finish_pipeline(false, null, volumes)
		return
	var root = _get_edited_scene_root(volumes)
	if not root:
		_log_and_show_error("No scene open", "Open a scene before baking.")
		_finish_pipeline(false, null, volumes)
		return
	var static_scene_node = _find_resonance_static_scene_for_bake(volumes)
	var static_asset = static_scene_node.get("static_scene_asset") if static_scene_node else null
	var baked_probe_datas: Array = []
	var tree = editor_interface.get_base_control().get_tree() if editor_interface else null
	var vol_index := 0
	for vol in volumes:
		if _progress_ui.cancel_requested:
			call_deferred("_on_bake_pipeline_finished", false, null, volumes)
			return
		vol_index += 1
		var ctx = _build_volume_bake_context(vol, root, vol_index, volumes.size(), static_asset)
		_progress_ui.set_stage(vol_index, volumes.size(), estimate_bake_time(vol) if vol_index == 1 else "")
		_progress_ui.set_bake_status(tr(UIStrings.PROGRESS_PROCESSING) + ctx.vol_info)
		if tree:
			await tree.process_frame
			await tree.create_timer(BAKE_VOLUME_DELAY_SEC).timeout
		srv.set_bake_params(ctx.bc.get_bake_params())
		var ok = await _run_bake_for_volume(ctx)
		if not ok:
			_log_and_show_error("Reflections bake failed for %s" % ctx.vol.name, "Check geometry and probe volume settings.", "bake_probes_for_volume returned false", ctx.vol.name, "reflections")
			_finish_pipeline(false, null, volumes)
			return
		if _progress_ui.cancel_requested:
			_finish_pipeline(false, null, volumes)
			return
		baked_probe_datas.append(vol.get_probe_data())
	_finish_pipeline(true, baked_probe_datas, volumes)

func _skip_if_up_to_date(ctx: VolumeBakeContext) -> bool:
	return not ctx.need_reflections and not ctx.need_pathing and not ctx.need_static_source and not ctx.need_static_listener

func _bake_reflections(ctx: VolumeBakeContext) -> bool:
	var srv = Engine.get_singleton("ResonanceServer")
	_progress_ui.set_bake_status(tr(UIStrings.PROGRESS_BAKING_REVERB) + ctx.vol_info)
	_prepare_probe_data_for_bake(ctx.vol, ctx.probe_data, ctx.root)
	var volume_transform = ctx.vol.global_transform
	var extents = ctx.vol.get("region_size") * 0.5
	var spacing = ctx.vol.get("spacing")
	var gen_type = ctx.vol.get("generation_type")
	var height = ctx.vol.get("height_above_floor")
	var do_bake = func() -> bool:
		return srv.bake_probes_for_volume(volume_transform, extents, spacing, gen_type, height, ctx.probe_data)
	await _run_in_thread_with_cancel_poll(do_bake)
	if _progress_ui.cancel_requested:
		return false
	ctx.probe_data = ctx.vol.get_probe_data()
	if not ctx.probe_data or ctx.probe_data.get_data().is_empty():
		if Engine.has_singleton("ResonanceLogger"):
			Engine.get_singleton("ResonanceLogger").log(&"bake", "Reflections bake returned empty data for %s" % ctx.vol.name, {"volume": ctx.vol.name, "step": "reflections"})
		return false
	if ctx.probe_data.has_method("set_pathing_params_hash"):
		ctx.probe_data.set_pathing_params_hash(0)
	if ctx.probe_data.has_method("set_static_source_params_hash"):
		ctx.probe_data.set_static_source_params_hash(0)
	if ctx.probe_data.has_method("set_static_listener_params_hash"):
		ctx.probe_data.set_static_listener_params_hash(0)
	if ctx.bc.pathing_enabled:
		ctx.need_pathing = true
	if ctx.static_asset and ctx.probe_data.has_method("set_static_scene_params_hash") and srv.has_method("get_geometry_asset_hash"):
		ctx.probe_data.set_static_scene_params_hash(srv.get_geometry_asset_hash(ctx.static_asset))
	return true

func _run_bake_step(ctx: VolumeBakeContext, status_key: String, bake_callable: Callable, hash_setter: String, hash_value: int) -> void:
	_progress_ui.set_bake_status(status_key + ctx.vol_info)
	var ok = await _run_in_thread_with_cancel_poll(bake_callable)
	if ok and ctx.probe_data.has_method(hash_setter):
		ctx.probe_data.call(hash_setter, hash_value)
	ctx.probe_data = ctx.vol.get_probe_data()

func _bake_pathing(ctx: VolumeBakeContext) -> void:
	if ctx.probe_data.get_data().is_empty():
		return
	var srv = Engine.get_singleton("ResonanceServer")
	var do_pathing = func() -> bool:
		return srv.bake_pathing(ctx.probe_data)
	await _run_bake_step(ctx, tr(UIStrings.PROGRESS_BAKING_PATHING), do_pathing, "set_pathing_params_hash", _compute_pathing_hash(ctx.bc))

func _bake_static_source(ctx: VolumeBakeContext) -> void:
	var srv = Engine.get_singleton("ResonanceServer")
	var do_static_source = func() -> bool:
		return srv.bake_static_source(ctx.probe_data, ctx.player_pos, ctx.player_radius)
	await _run_bake_step(ctx, tr(UIStrings.PROGRESS_BAKING_STATIC_SOURCE), do_static_source, "set_static_source_params_hash", _compute_position_radius_hash(ctx.player_pos, ctx.player_radius))

func _bake_static_listener(ctx: VolumeBakeContext) -> void:
	var srv = Engine.get_singleton("ResonanceServer")
	var do_static_listener = func() -> bool:
		return srv.bake_static_listener(ctx.probe_data, ctx.listener_pos, ctx.listener_radius)
	await _run_bake_step(ctx, tr(UIStrings.PROGRESS_BAKING_STATIC_LISTENER), do_static_listener, "set_static_listener_params_hash", _compute_position_radius_hash(ctx.listener_pos, ctx.listener_radius))

func _run_bake_for_volume(ctx: VolumeBakeContext) -> bool:
	var srv = Engine.get_singleton("ResonanceServer")
	srv.set_bake_params(ctx.bc.get_bake_params())
	srv.set_bake_pipeline_pathing(ctx.need_pathing)
	if _skip_if_up_to_date(ctx):
		_progress_ui.set_bake_status(tr(UIStrings.PROGRESS_SKIPPING) + ctx.vol_info)
		return true
	if not await _bake_reflections(ctx):
		return false
	if ctx.need_pathing and ctx.bc.pathing_enabled:
		await _bake_pathing(ctx)
	if ctx.need_static_source and ctx.add_flags.static_source:
		await _bake_static_source(ctx)
	if ctx.need_static_listener and ctx.add_flags.static_listener:
		await _bake_static_listener(ctx)
	return true

func _on_bake_pipeline_finished(success: bool, probe_data_ref, volumes: Array[Node]) -> void:
	_bake_in_progress = false
	var srv = Engine.get_singleton("ResonanceServer") if Engine.has_singleton("ResonanceServer") else null
	if srv and srv.has_method("set_bake_pipeline_pathing"):
		srv.set_bake_pipeline_pathing(false)
	if success and probe_data_ref:
		_progress_ui.set_bake_status(tr("Saving probe data..."))
		match probe_data_ref:
			var arr when arr is Array:
				for pd in arr:
					_save_and_reload_probe_data(pd, volumes)
			_:
				_save_and_reload_probe_data(probe_data_ref, volumes)
		_show_bake_complete_dialog(volumes)
	elif not success and not _progress_ui.cancel_requested:
		_log_and_show_error("Bake failed.", "Check Editor output and ensure geometry is valid.")
	_progress_ui.hide_ui()
	if editor_interface:
		editor_interface.mark_scene_as_unsaved()
	_notify_volumes_viz_updated(volumes)

func _save_and_reload_probe_data(probe_data_ref: Resource, volumes: Array[Node]) -> void:
	var path = probe_data_ref.resource_path
	if not path.is_empty():
		var err = ResourceSaver.save(probe_data_ref)
		if err != OK:
			var vol_name := volumes[0].name if volumes.size() > 0 else "?"
			if Engine.has_singleton("ResonanceLogger"):
				Engine.get_singleton("ResonanceLogger").log(&"bake", "Failed to save probe data: %s" % err, {"volume": vol_name, "step": "save", "error_code": err})
			ResonanceEditorDialogs.show_error_dialog(editor_interface, tr(UIStrings.DIALOG_SAVE_FAILED_TITLE), tr(UIStrings.ERR_FAILED_TO_SAVE) % err, "ResourceSaver.save returned non-OK.", "Ensure res://audio_data/ is writable.")
	_reload_volumes_using_probe_data(probe_data_ref, volumes)

func _reload_volumes_using_probe_data(probe_data_ref: Resource, volumes: Array[Node]) -> void:
	var root = _get_edited_scene_root(volumes)
	if not root:
		return
	var collected: Array[Node] = []
	ResonanceSceneUtils.collect_resonance_probe_volumes(root, collected)
	var all_volumes: Array[Node] = []
	for v in collected:
		all_volumes.append(v)
	for vol in all_volumes:
		if vol.has_method("get_probe_data") and vol.get_probe_data() == probe_data_ref:
			if vol.has_method("reload_probe_batch"):
				vol.reload_probe_batch()
	_notify_volumes_viz_updated(all_volumes)

func _notify_volumes_viz_updated(volumes: Array[Node]) -> void:
	var root = _get_edited_scene_root(volumes)
	if not root:
		return
	var cfg = _find_resonance_runtime(root)
	if not cfg:
		return
	var rt = cfg.get("runtime") if cfg else null
	var refl: int = rt.get("reflection_type") if rt else 0
	var pathing: bool = rt.get("pathing_enabled") if rt else false
	for vol in volumes:
		if vol.has_method("notify_runtime_config_changed"):
			vol.notify_runtime_config_changed(refl, pathing)

## Bake complete dialog with Undo option when backup exists.
func _show_bake_complete_dialog(volumes: Array) -> void:
	if not editor_interface:
		return
	var base = editor_interface.get_base_control()
	if not base:
		return
	var has_backups = _backup.has_backups()
	var msg = "Bake completed for %d Probe Volume(s)." % volumes.size()
	if has_backups:
		msg += "\n\nYou can Undo to restore the previous Probe Volume data."
	var dialog = AcceptDialog.new()
	dialog.title = tr(UIStrings.ADDON_NAME)
	dialog.dialog_text = msg
	dialog.theme = editor_interface.get_editor_theme()
	dialog.confirmed.connect(dialog.queue_free)
	dialog.close_requested.connect(dialog.queue_free)
	if has_backups:
		var undo_btn = dialog.add_button(tr(UIStrings.BTN_UNDO), false, "undo")
		undo_btn.pressed.connect(func():
			_backup.restore(volumes, editor_interface, _reload_volumes_using_probe_data, func(): _on_restore_complete(volumes))
			dialog.queue_free()
		)
	base.add_child(dialog)
	dialog.popup_centered()

func _on_restore_complete(volumes: Array) -> void:
	if editor_interface:
		editor_interface.mark_scene_as_unsaved()
	_notify_volumes_viz_updated(volumes)

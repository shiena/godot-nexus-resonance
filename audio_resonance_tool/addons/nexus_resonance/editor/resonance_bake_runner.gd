@tool
extends RefCounted
class_name ResonanceBakeRunner

## Shared bake logic for ResonanceProbeVolume. Used by inspector plugin and Project menu.
## Run bake for selected volume(s) or all volumes in scene. Hash-based incremental baking.
## Heavy logic lives in resonance_bake_*.gd modules; this class is the public facade.

const ResonanceBakeConfig = preload("res://addons/nexus_resonance/scripts/resonance_bake_config.gd")
const ResonanceRuntimeScript = preload("res://addons/nexus_resonance/scripts/resonance_runtime.gd")
const ResonanceSceneUtils = preload("res://addons/nexus_resonance/scripts/resonance_scene_utils.gd")
const _BakeEstimates = preload("res://addons/nexus_resonance/editor/resonance_bake_estimates.gd")
const _BakeHashes = preload("res://addons/nexus_resonance/editor/resonance_bake_hashes.gd")
const _BakeDiscovery = preload("res://addons/nexus_resonance/editor/resonance_bake_discovery.gd")
const _BakeValidation = preload("res://addons/nexus_resonance/editor/resonance_bake_validation.gd")
const _BakeServerSetup = preload("res://addons/nexus_resonance/editor/resonance_bake_server_setup.gd")
const _BakePipeline = preload("res://addons/nexus_resonance/editor/resonance_bake_pipeline.gd")
const ResonanceEditorDialogs = preload(
	"res://addons/nexus_resonance/editor/resonance_editor_dialogs.gd"
)
const ResonanceBakeProgressUI = preload(
	"res://addons/nexus_resonance/editor/resonance_bake_progress_ui.gd"
)
const ResonanceBakeBackup = preload("res://addons/nexus_resonance/editor/resonance_bake_backup.gd")
const UIStrings = preload("res://addons/nexus_resonance/scripts/resonance_ui_strings.gd")

const DEFAULT_BAKE_INFLUENCE_RADIUS := 10000.0

var editor_interface: EditorInterface
## When set, used as quick link in validation dialog when static scene not exported.
var export_static_callback: Callable = Callable()
var _bake_in_progress: bool = false

var _progress_ui: ResonanceBakeProgressUI
var _backup: ResonanceBakeBackup
var _server_setup: RefCounted
var _pipeline: RefCounted


func _init(p_editor_interface: EditorInterface) -> void:
	editor_interface = p_editor_interface
	_progress_ui = ResonanceBakeProgressUI.new(p_editor_interface)
	_backup = ResonanceBakeBackup.new()
	_server_setup = _BakeServerSetup.new(self)
	_pipeline = _BakePipeline.new(self)


func run_bake(volumes: Array[Node]) -> void:
	if volumes.is_empty() or _bake_in_progress:
		return
	var root = _get_edited_scene_root(volumes)
	if not root:
		_log_and_show_error("No scene open", "Open a scene before baking.")
		return
	var static_scene_node = _BakeDiscovery.find_resonance_static_scene_for_bake(volumes, root)
	var static_asset = static_scene_node.get("static_scene_asset") if static_scene_node else null
	_do_run_bake_with_backup(volumes, root, static_scene_node, static_asset)


func _do_run_bake_with_backup(
	volumes: Array[Node], root: Node, static_scene_node: Node, static_asset
) -> void:
	_backup.create_backups(volumes)
	_do_run_bake_after_validation(volumes, root, static_scene_node, static_asset, [])


func _do_run_bake_after_validation(
	volumes: Array[Node], root: Node, static_scene_node: Node, static_asset, _checklist: Array
) -> void:
	if not static_scene_node or not static_scene_node.has_valid_asset():
		_log_and_show_error(
			"Static scene not exported",
			"Use Tools > Nexus Resonance > Export Static Scene before baking.",
			"Scene must be exported first."
		)
		return
	if not ResonanceServerAccess.has_server():
		_log_and_show_error(
			"GDExtension not loaded", "The ResonanceServer GDExtension is not available."
		)
		return
	if not _server_setup.ensure_resonance_server_initialized(volumes):
		return
	var srv = ResonanceServerAccess.get_server()
	if srv and srv.has_method("set_bake_static_scene_asset"):
		srv.set_bake_static_scene_asset(static_asset)
	_bake_in_progress = true
	_progress_ui.show_ui()
	var base_ctrl = editor_interface.get_base_control() if editor_interface else null
	var tree: SceneTree = base_ctrl.get_tree() if base_ctrl else null
	if tree:
		var cb = func() -> void: _run_bake_pipeline_main_thread(volumes)
		tree.process_frame.connect(cb, CONNECT_ONE_SHOT)
	else:
		call_deferred("_run_bake_pipeline_main_thread", volumes)


func _get_edited_scene_root(volumes: Array[Node]) -> Node:
	return _BakeValidation.get_edited_scene_root(volumes, editor_interface)


## Estimates probe count for a volume. Uses region_size, spacing, generation_type.
func estimate_probe_count(vol: Node) -> int:
	return _BakeEstimates.estimate_probe_count(vol)


## Rough bake time estimate in seconds. Based on probe count and bake config.
func estimate_bake_time(vol: Node) -> String:
	return _BakeEstimates.estimate_bake_time(vol, _get_bake_config_for_volume(vol))


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
	var path_hash = _BakeHashes.compute_pathing_hash(bc) if want_path else 0
	var ph = (
		probe_data.get_pathing_params_hash()
		if probe_data.has_method("get_pathing_params_hash")
		else 0
	)
	var desired_refl = bc.reflection_type
	var refl_matches = (
		probe_data.get_baked_reflection_type() == desired_refl
		if probe_data.has_method("get_baked_reflection_type")
		else false
	)
	var hash_matches = (
		probe_data.get_bake_params_hash() == vol.get_bake_params_hash()
		if vol.has_method("get_bake_params_hash")
		else false
	)
	if not want_path and ph > 0:
		return "Outdated"
	if not hash_matches or not refl_matches:
		return "Outdated"
	if want_path and (ph == 0 or ph != path_hash):
		return "Outdated"
	var vols: Array[Node] = []
	vols.append(vol)
	var root = _get_edited_scene_root(vols)
	var static_scene_node = _BakeDiscovery.find_resonance_static_scene_for_bake(vols, root)
	var static_asset = static_scene_node.get("static_scene_asset") if static_scene_node else null
	if static_asset and ResonanceServerAccess.has_server():
		var srv = ResonanceServerAccess.get_server()
		if (
			srv.has_method("get_geometry_asset_hash")
			and probe_data.has_method("get_static_scene_params_hash")
		):
			var current_hash = srv.get_geometry_asset_hash(static_asset)
			var stored = probe_data.get_static_scene_params_hash()
			if current_hash != 0 and (stored == 0 or stored != current_hash):
				return "Outdated"
	if bc.static_source_enabled and root:
		var src = _BakeDiscovery.resolve_bake_node_for_volume(
			vol, root, "bake_sources", "ResonancePlayer"
		)
		if src and src is Node3D:
			var rad = (
				vol.get("bake_influence_radius")
				if "bake_influence_radius" in vol
				else DEFAULT_BAKE_INFLUENCE_RADIUS
			)
			var sh = _BakeHashes.compute_position_radius_hash(src.global_position, rad)
			var ssh = (
				probe_data.get_static_source_params_hash()
				if probe_data.has_method("get_static_source_params_hash")
				else 0
			)
			if ssh == 0 or ssh != sh:
				return "Outdated"
	if bc.static_listener_enabled and root:
		var lst = _BakeDiscovery.resolve_bake_node_for_volume(
			vol, root, "bake_listeners", "ResonanceListener"
		)
		if lst and lst is Node3D:
			var rad = (
				vol.get("bake_influence_radius")
				if "bake_influence_radius" in vol
				else DEFAULT_BAKE_INFLUENCE_RADIUS
			)
			var lh = _BakeHashes.compute_position_radius_hash(lst.global_position, rad)
			var lsh = (
				probe_data.get_static_listener_params_hash()
				if probe_data.has_method("get_static_listener_params_hash")
				else 0
			)
			if lsh == 0 or lsh != lh:
				return "Outdated"
	return "Probes baked"


## Ensures ResonanceServer is initialized and refreshes probe visuals.
## Call when a volume is selected in the inspector (so show_probes works without baking first).
func ensure_resonance_server_for_volumes(volumes: Array[Node]) -> bool:
	if not _server_setup.ensure_resonance_server_initialized(volumes):
		return false
	_notify_volumes_viz_updated(volumes)
	return true


func _log_and_show_error(
	message: String,
	solution: String = "",
	cause: String = "",
	volume_name: String = "",
	step: String = ""
) -> void:
	_server_setup.log_and_show_error(message, solution, cause, volume_name, step)


func _get_bake_config_for_volume(vol) -> Resource:
	if vol and vol.has_method("get_bake_config"):
		var bc = vol.get_bake_config()
		if bc:
			return bc
	return ResonanceBakeConfig.create_default()


func _run_bake_pipeline_main_thread(volumes: Array[Node]) -> void:
	await _pipeline.run_bake_pipeline_main_thread(volumes)


func _finish_pipeline(success: bool, probe_data_ref, volumes: Array[Node]) -> void:
	call_deferred("_on_bake_pipeline_finished", success, probe_data_ref, volumes)


func _on_bake_pipeline_finished(success: bool, probe_data_ref, volumes: Array[Node]) -> void:
	_bake_in_progress = false
	var srv = ResonanceServerAccess.get_server()
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
				Engine.get_singleton("ResonanceLogger").log(
					&"bake",
					"Failed to save probe data: %s" % err,
					{"volume": vol_name, "step": "save", "error_code": err}
				)
			ResonanceEditorDialogs.show_error_dialog(
				editor_interface,
				tr(UIStrings.DIALOG_SAVE_FAILED_TITLE),
				tr(UIStrings.ERR_FAILED_TO_SAVE) % err,
				"ResourceSaver.save returned non-OK.",
				"Ensure res://audio_data/ is writable."
			)
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
	var cfg = _BakeDiscovery.find_resonance_runtime(root, ResonanceRuntimeScript)
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
		undo_btn.pressed.connect(
			func():
				_backup.restore(
					volumes,
					editor_interface,
					_reload_volumes_using_probe_data,
					func(): _on_restore_complete(volumes)
				)
				dialog.queue_free()
		)
	base.add_child(dialog)
	dialog.popup_centered()


func _on_restore_complete(volumes: Array) -> void:
	if editor_interface:
		editor_interface.mark_scene_as_unsaved()
	_notify_volumes_viz_updated(volumes)

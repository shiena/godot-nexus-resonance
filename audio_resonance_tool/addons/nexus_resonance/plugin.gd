@tool
extends EditorPlugin

const LOGGER_SCRIPT = "res://addons/nexus_resonance/scripts/resonance_logger.tscn"
const GIZMO_SCRIPT = "res://addons/nexus_resonance/editor/resonance_probe_gizmo.gd"

const BUS_NAME = "ResonanceReverb"
const EFFECT_CLASS = "ResonanceAudioEffect"
const PROBE_DATA_SAVER_SCRIPT = "res://addons/nexus_resonance/editor/resonance_probe_data_saver.gd"
const PROBE_DATA_LOADER_SCRIPT = "res://addons/nexus_resonance/editor/resonance_probe_data_loader.gd"
const SOFA_IMPORTER_SCRIPT = "res://addons/nexus_resonance/editor/sofa_importer.gd"
const RESONANCE_RUNTIME_SCRIPT = "res://addons/nexus_resonance/scripts/resonance_runtime.gd"
const RESONANCE_CONFIG_ICON = "res://addons/nexus_resonance/ui/icons/resonance_config_icon.svg"
const RESONANCE_GEOMETRY_INSPECTOR_SCRIPT = "res://addons/nexus_resonance/editor/resonance_geometry_inspector.gd"
const RESONANCE_BAKE_RUNNER_SCRIPT = "res://addons/nexus_resonance/editor/resonance_bake_runner.gd"
const RESONANCE_PROBE_VOLUME_INSPECTOR_SCRIPT = "res://addons/nexus_resonance/editor/resonance_probe_volume_inspector.gd"

const ResonancePaths = preload("res://addons/nexus_resonance/scripts/resonance_paths.gd")
const ResonanceSceneUtils = preload("res://addons/nexus_resonance/scripts/resonance_scene_utils.gd")
const UIStrings = preload("res://addons/nexus_resonance/scripts/resonance_ui_strings.gd")
const ResonanceEditorDialogs = preload("res://addons/nexus_resonance/editor/resonance_editor_dialogs.gd")

var gizmo_instance = null
var resonance_geometry_inspector = null
var resonance_probe_volume_inspector = null
var bake_runner = null  # ResonanceBakeRunner
var sofa_importer = null
var probe_data_saver = null
var probe_data_loader = null
const _tool_submenu_name := "Nexus Resonance"
var _tool_submenu: PopupMenu = null

func _enter_tree():
	# Bake settings moved to ResonanceRuntimeConfig; no ProjectSettings registration (DRY)
	_register_logger_project_settings()
	# Register custom ResourceFormatSaver for ResonanceProbeData
	var saver_script = load(PROBE_DATA_SAVER_SCRIPT)
	if saver_script:
		var saver = saver_script.new()
		if saver:
			ResourceSaver.add_resource_format_saver(saver, true)
			probe_data_saver = saver
	# Register custom ResourceFormatLoader for .resonance_probe (raw bake output when ResourceSaver fails)
	var loader_script = load(PROBE_DATA_LOADER_SCRIPT)
	if loader_script:
		var loader = loader_script.new()
		if loader:
			ResourceLoader.add_resource_format_loader(loader, true)
			probe_data_loader = loader
	# 1. Bake runner and Probe Volume inspector
	var runner_script = load(RESONANCE_BAKE_RUNNER_SCRIPT)
	if runner_script:
		bake_runner = runner_script.new(get_editor_interface())
		bake_runner.export_static_callback = _on_tool_export_meshes
	var pv_inspector_script = load(RESONANCE_PROBE_VOLUME_INSPECTOR_SCRIPT) as Script
	if pv_inspector_script and bake_runner:
		resonance_probe_volume_inspector = pv_inspector_script.new()
		resonance_probe_volume_inspector.bake_runner = bake_runner
		resonance_probe_volume_inspector.editor_interface = get_editor_interface()
		add_inspector_plugin(resonance_probe_volume_inspector)

	# 2. Gizmo
	if FileAccess.file_exists(GIZMO_SCRIPT):
		var script = load(GIZMO_SCRIPT)
		if script:
			var inst = script.new()
			if inst:
				gizmo_instance = inst
				if "undo_redo" in gizmo_instance:
					gizmo_instance.undo_redo = get_undo_redo()
				add_node_3d_gizmo_plugin(gizmo_instance)

	_tool_submenu = PopupMenu.new()
	var base = get_editor_interface().get_base_control()
	var icon_export = ResonanceEditorDialogs.get_icon(base, UIStrings.ICON_EXPORT, "Export")
	var icon_bake = ResonanceEditorDialogs.get_icon(base, UIStrings.ICON_BAKE, "Bake")
	var icon_clear = ResonanceEditorDialogs.get_icon(base, UIStrings.ICON_CLEAR, "Clear")
	_tool_submenu.add_icon_item(icon_export, UIStrings.MENU_EXPORT_STATIC_SCENE, 0)
	_tool_submenu.add_icon_item(icon_export, UIStrings.MENU_EXPORT_DYNAMIC_MESHES, 1)
	_tool_submenu.add_icon_item(icon_bake, UIStrings.MENU_BAKE_ALL_PROBE_VOLUMES, 2)
	_tool_submenu.add_icon_item(icon_clear, UIStrings.MENU_CLEAR_PROBE_BATCHES, 3)
	_tool_submenu.add_item(UIStrings.MENU_UNLINK_PROBE_VOLUME_REFS, 4)
	_tool_submenu.id_pressed.connect(_on_tool_submenu_id_pressed)
	_register_tool_shortcuts()
	add_tool_submenu_item(_tool_submenu_name, _tool_submenu)

	# SOFA HRTF importer
	var importer_script = load(SOFA_IMPORTER_SCRIPT)
	if importer_script:
		sofa_importer = importer_script.new()
		if sofa_importer:
			add_import_plugin(sofa_importer)

	# Register ResonanceRuntime for Add Child Node
	add_custom_type("ResonanceRuntime", "Node", load(RESONANCE_RUNTIME_SCRIPT) as Script, load(RESONANCE_CONFIG_ICON) as Texture2D)

	# ResonanceGeometry inspector with Export button
	var inspector_script = load(RESONANCE_GEOMETRY_INSPECTOR_SCRIPT) as Script
	if inspector_script:
		resonance_geometry_inspector = inspector_script.new()
		resonance_geometry_inspector.editor_interface = get_editor_interface()
		add_inspector_plugin(resonance_geometry_inspector)

func _exit_tree():
	# Remove Resonance Reverb effect from bus first to stop audio thread from calling into native code during shutdown
	_detach_reverb_effect()
	# Do NOT call ResourceSaver.remove_resource_format_saver/ResourceLoader.remove_resource_format_loader:
	# when closing the editor, Godot may tear these down before _exit_tree, so the saver/loader is already
	# gone. Calling remove then triggers "i >= saver_count" and SIGSEGV. Engine handles cleanup on exit.
	# Plugin disable+re-enable may add duplicates; acceptable tradeoff.
	probe_data_saver = null
	probe_data_loader = null
	remove_custom_type("ResonanceRuntime")
	if resonance_geometry_inspector:
		remove_inspector_plugin(resonance_geometry_inspector)
		resonance_geometry_inspector = null
	if resonance_probe_volume_inspector:
		remove_inspector_plugin(resonance_probe_volume_inspector)
		resonance_probe_volume_inspector = null
	bake_runner = null
	remove_tool_menu_item(_tool_submenu_name)
	_tool_submenu = null
	if sofa_importer:
		remove_import_plugin(sofa_importer)
		sofa_importer = null
	if gizmo_instance: remove_node_3d_gizmo_plugin(gizmo_instance)

func _enable_plugin():
	add_autoload_singleton("ResonanceLogger", LOGGER_SCRIPT)
	call_deferred("_setup_audio_bus")

func _disable_plugin():
	# Do NOT call ResonanceServer.clear_probe_batches here: on editor close, the GDExtension may be
	# unloaded before _disable_plugin runs. Calling into native code then causes SIGSEGV. The
	# ResonanceServer destructor already cleans probe batches via _shutdown_steam_audio().
	remove_autoload_singleton("ResonanceLogger")
	# Optional: Remove Audio Bus? Left out for user convenience.

func _detach_reverb_effect() -> void:
	var idx = AudioServer.get_bus_index(BUS_NAME)
	if idx < 0:
		return
	var n = AudioServer.get_bus_effect_count(idx)
	for i in range(n - 1, -1, -1):
		var eff = AudioServer.get_bus_effect(idx, i)
		if eff and eff.get_class() == EFFECT_CLASS:
			AudioServer.remove_bus_effect(idx, i)
			break

func _setup_audio_bus():
	var idx = AudioServer.get_bus_index(BUS_NAME)
	if idx == -1:
		AudioServer.add_bus()
		idx = AudioServer.bus_count - 1
		AudioServer.set_bus_name(idx, BUS_NAME)
		if idx > 1:
			AudioServer.move_bus(idx, 1)
			idx = 1
		if ClassDB.class_exists(EFFECT_CLASS):
			var effect = ClassDB.instantiate(EFFECT_CLASS)
			effect.resource_name = "Resonance Reverb"
			AudioServer.add_bus_effect(idx, effect)
	# Ensure bus outputs to Master (critical for convolution reverb to reach speakers)
	AudioServer.set_bus_send(idx, &"Master")

func _has_main_screen(): return false 
func _get_plugin_name(): return "Resonance"
func _get_plugin_icon(): return get_editor_interface().get_base_control().get_theme_icon("AudioStreamPlayer", "EditorIcons")
func _register_logger_project_settings() -> void:
	const PREFIX := "audio/nexus_resonance/logger/"
	if not ProjectSettings.has_setting(PREFIX + "categories_enabled"):
		ProjectSettings.set_setting(PREFIX + "categories_enabled", {})
	if not ProjectSettings.has_setting(PREFIX + "output_to_file"):
		ProjectSettings.set_setting(PREFIX + "output_to_file", false)
	if not ProjectSettings.has_setting(PREFIX + "file_path"):
		ProjectSettings.set_setting(PREFIX + "file_path", "user://nexus_resonance_log.ndjson")

func _register_tool_shortcuts() -> void:
	var sc_static = Shortcut.new()
	var ev_static = InputEventKey.new()
	ev_static.keycode = KEY_E
	ev_static.ctrl_pressed = true
	ev_static.shift_pressed = true
	ev_static.command_or_control_autoremap = true
	sc_static.events = [ev_static]
	_tool_submenu.set_item_shortcut(0, sc_static, true)

	var sc_dynamic = Shortcut.new()
	var ev_dynamic = InputEventKey.new()
	ev_dynamic.keycode = KEY_D
	ev_dynamic.ctrl_pressed = true
	ev_dynamic.shift_pressed = true
	ev_dynamic.command_or_control_autoremap = true
	sc_dynamic.events = [ev_dynamic]
	_tool_submenu.set_item_shortcut(1, sc_dynamic, true)

	var sc_bake = Shortcut.new()
	var ev_bake = InputEventKey.new()
	ev_bake.keycode = KEY_B
	ev_bake.ctrl_pressed = true
	ev_bake.shift_pressed = true
	ev_bake.command_or_control_autoremap = true
	sc_bake.events = [ev_bake]
	_tool_submenu.set_item_shortcut(2, sc_bake, true)

func _on_tool_submenu_id_pressed(id: int) -> void:
	match id:
		0: _on_tool_export_meshes(null)
		1: _on_tool_export_dynamic_mesh(null)
		2: _on_tool_bake_all_probe_volumes(null)
		3: _on_tool_clear_probe_batches(null)
		4: _on_tool_unlink_probe_refs(null)

func _on_tool_export_meshes(_ud = null):
	## Export static ResonanceGeometry (dynamic=false) to merged asset. Creates/updates ResonanceStaticScene.
	## Skips export when geometry unchanged (hash). Clears asset reference before re-export to avoid cyclic resource error.
	var root = get_editor_interface().get_edited_scene_root()
	if not root:
		_show_warning("No scene open.", "Open a scene before exporting static geometry.")
		return
	if not Engine.has_singleton("ResonanceServer"):
		_show_gdextension_error()
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if not srv.has_method("export_static_scene_to_asset"):
		ResonanceEditorDialogs.show_error_dialog(
			get_editor_interface(),
			UIStrings.DIALOG_EXPORT_FAILED_TITLE,
			UIStrings.ERR_SERVER_LACKS_EXPORT,
			"Addon and GDExtension may be out of sync.",
			"Update the Nexus Resonance addon and GDExtension to matching versions."
		)
		return
	var scene_name := "unsaved"
	var scene_path: String = root.get_scene_file_path()
	if not scene_path.is_empty():
		scene_name = scene_path.get_file().get_basename()
	if not DirAccess.dir_exists_absolute(ResonancePaths.PATH_AUDIO_DATA):
		DirAccess.make_dir_recursive_absolute(ResonancePaths.PATH_AUDIO_DATA)
	var save_path := ResonancePaths.PATH_AUDIO_DATA + scene_name + "_static.tres"

	var static_scene_node = ResonanceSceneUtils.find_resonance_static_scene(root)
	var current_hash: int = srv.get_static_scene_hash(root) if srv.has_method("get_static_scene_hash") else 0
	# Only skip when hash matches AND we have a valid asset AND the target file still exists
	if static_scene_node and static_scene_node.get("export_hash") == current_hash and current_hash != 0:
		var has_valid = static_scene_node.has_method("has_valid_asset") and static_scene_node.has_valid_asset()
		var file_exists = FileAccess.file_exists(save_path)
		if has_valid and file_exists:
			ResonanceEditorDialogs.show_info(UIStrings.INFO_STATIC_UNCHANGED)
			return
	# Release old asset reference so ResourceSaver can overwrite the path (avoids cyclic resource inclusion)
	if static_scene_node and static_scene_node.get("static_scene_asset"):
		static_scene_node.set("static_scene_asset", null)

	var err = srv.export_static_scene_to_asset(root, save_path)
	if err != OK:
		ResonanceEditorDialogs.show_critical(get_editor_interface(), UIStrings.ERR_EXPORT_FAILED % err, UIStrings.DIALOG_EXPORT_FAILED_TITLE)
		return
	get_editor_interface().get_resource_filesystem().scan()
	if not static_scene_node:
		static_scene_node = ClassDB.instantiate("ResonanceStaticScene")
		static_scene_node.name = "ResonanceStaticScene"
		root.add_child(static_scene_node)
		static_scene_node.owner = root
	var asset = load(save_path) as Resource
	if asset:
		static_scene_node.set("static_scene_asset", asset)
		static_scene_node.set("scene_name_when_exported", scene_name)
		static_scene_node.set("export_hash", current_hash)
		get_editor_interface().mark_scene_as_unsaved()
	ResonanceEditorDialogs.show_success_toast(get_editor_interface(), UIStrings.INFO_STATIC_EXPORTED % save_path)

func _show_warning(message: String, _solution: String = "") -> void:
	ResonanceEditorDialogs.show_warning(get_editor_interface(), message)

func _show_gdextension_error() -> void:
	ResonanceEditorDialogs.show_critical(get_editor_interface(), UIStrings.ERR_GDEXTENSION_NOT_LOADED, UIStrings.DIALOG_GDEXTENSION_NOT_LOADED_TITLE)

func _on_tool_export_dynamic_mesh(_ud = null):
	## Export all ResonanceDynamicGeometry nodes to mesh assets. Centralized like static export.
	var root = get_editor_interface().get_edited_scene_root()
	if not root:
		_show_warning("No scene open.", "Open a scene before exporting dynamic meshes.")
		return
	var dynamic_geoms: Array = []
	ResonanceSceneUtils.collect_resonance_dynamic_geometry(root, dynamic_geoms)
	if dynamic_geoms.is_empty():
		_show_warning("No ResonanceDynamicGeometry in scene.", "Add ResonanceDynamicGeometry nodes to export.")
		return
	if not DirAccess.dir_exists_absolute(ResonancePaths.PATH_RESONANCE_MESHES):
		DirAccess.make_dir_recursive_absolute(ResonancePaths.PATH_RESONANCE_MESHES)
	var exported := 0
	for geom in dynamic_geoms:
		var parent_name: String = geom.get_parent().name if geom.get_parent() else "mesh"
		var save_path := ResonancePaths.PATH_RESONANCE_MESHES + parent_name.to_snake_case() + "_dynamic.tres"
		var err = geom.export_dynamic_mesh_to_asset(save_path)
		if err == OK:
			exported += 1
	if exported > 0:
		get_editor_interface().get_resource_filesystem().scan()
		get_editor_interface().mark_scene_as_unsaved()
		ResonanceEditorDialogs.show_success_toast(get_editor_interface(), UIStrings.INFO_DYNAMIC_MESHES_EXPORTED % exported)

func _on_tool_unlink_probe_refs(_ud = null):
	## Workaround for Godot engine bug: Deleting a probe volume that is still referenced by
	## ResonancePlayer.pathing_probe_volume can trigger:
	##   ERROR: core/string/node_path.cpp:272 - Condition "!p_np.is_absolute()" is true. Returning: NodePath()
	## Select the Probe Volume(s) to remove, run this, then delete. Refs are also auto-cleared on EXIT_TREE.
	var root = get_editor_interface().get_edited_scene_root()
	if not root:
		_show_warning("No scene open.", "Open a scene first.")
		return
	var selected_volumes: Array[Node] = []
	for n in get_editor_interface().get_selection().get_selected_nodes():
		if n.is_class("ResonanceProbeVolume"):
			selected_volumes.append(n)
	if selected_volumes.is_empty():
		_show_warning("No ResonanceProbeVolume selected.", "Select one or more ResonanceProbeVolume nodes, then run this.")
		return
	var ur = get_undo_redo()
	ur.create_action("Nexus Resonance: Unlink Probe Volume References")
	var result := { "count": 0, "nodes": [] }
	_collect_probe_refs_to_clear(root, root, selected_volumes, result)
	if result["count"] == 0:
		_show_warning("No references found.", "No ResonancePlayer in this scene points to the selected Probe Volume(s).")
		return
	var nodes_to_clear: Array = result["nodes"]
	for n in nodes_to_clear:
		var old_path: NodePath = n.get_pathing_probe_volume()
		ur.add_do_property(n, "pathing_probe_volume", NodePath())
		ur.add_undo_property(n, "pathing_probe_volume", old_path)
	ur.commit_action()
	ResonanceEditorDialogs.show_success_toast(get_editor_interface(), UIStrings.INFO_UNLINK_DONE % result["count"])

func _collect_probe_refs_to_clear(node: Node, scene_root: Node, targets: Array, result: Dictionary) -> void:
	if node.has_method("get_pathing_probe_volume") and node.has_method("set_pathing_probe_volume"):
		var path: NodePath = node.get_pathing_probe_volume()
		if not path.is_empty():
			var target = scene_root.get_node_or_null(path)
			if target and target in targets:
				result["nodes"].append(node)
				result["count"] += 1
	for c in node.get_children():
		_collect_probe_refs_to_clear(c, scene_root, targets, result)

func _on_tool_bake_all_probe_volumes(_ud = null):
	var root = get_editor_interface().get_edited_scene_root()
	if not root:
		_show_warning("No scene open.", "Open a scene before baking.")
		return
	var volumes: Array[Node] = []
	ResonanceSceneUtils.collect_resonance_probe_volumes(root, volumes)
	if volumes.is_empty():
		_show_warning("No ResonanceProbeVolume in scene.", "Add ResonanceProbeVolume nodes to bake.")
		return
	if not bake_runner:
		ResonanceEditorDialogs.show_critical(get_editor_interface(), UIStrings.ERR_BAKE_RUNNER_NOT_INIT)
		return
	bake_runner.run_bake(volumes)

func _on_tool_clear_probe_batches(_ud = null):
	if Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv and srv.has_method("clear_probe_batches"):
			srv.clear_probe_batches()
			ResonanceEditorDialogs.show_info(UIStrings.INFO_PROBE_BATCHES_CLEARED)
	else:
		_show_gdextension_error()

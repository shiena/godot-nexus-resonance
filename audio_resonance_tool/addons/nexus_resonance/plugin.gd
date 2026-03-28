@tool
extends EditorPlugin

const LOGGER_AUTOLOAD_PATH = "res://addons/nexus_resonance/scripts/resonance_logger.tscn"
const GIZMO_SCRIPT = "res://addons/nexus_resonance/editor/resonance_probe_gizmo.gd"

const BUS_NAME = "ResonanceReverb"
const EFFECT_CLASS = "ResonanceAudioEffect"
const PROBE_DATA_SAVER_SCRIPT = "res://addons/nexus_resonance/editor/resonance_probe_data_saver.gd"
const PROBE_DATA_LOADER_SCRIPT = "res://addons/nexus_resonance/editor/resonance_probe_data_loader.gd"
const SOFA_IMPORTER_SCRIPT = "res://addons/nexus_resonance/editor/sofa_importer.gd"
const RESONANCE_GEOMETRY_INSPECTOR_SCRIPT = "res://addons/nexus_resonance/editor/resonance_geometry_inspector.gd"
const RESONANCE_BAKE_RUNNER_SCRIPT = "res://addons/nexus_resonance/editor/resonance_bake_runner.gd"
const RESONANCE_PROBE_VOLUME_INSPECTOR_SCRIPT = "res://addons/nexus_resonance/editor/resonance_probe_volume_inspector.gd"
const RESONANCE_EXPORT_HANDLER_SCRIPT = "res://addons/nexus_resonance/editor/resonance_export_handler.gd"
const RESONANCE_EXPORT_PLUGIN_SCRIPT = "res://addons/nexus_resonance/editor/nexus_resonance_export_plugin.gd"
const RESONANCE_FMOD_EVENT_EMITTER_INSPECTOR_SCRIPT = "res://addons/nexus_resonance/editor/resonance_fmod_event_emitter_inspector.gd"
const ResonanceSceneUtils = preload("res://addons/nexus_resonance/scripts/resonance_scene_utils.gd")
const UIStrings = preload("res://addons/nexus_resonance/scripts/resonance_ui_strings.gd")
const ResonanceLoggerScript = preload("res://addons/nexus_resonance/scripts/resonance_logger.gd")
const ResonanceEditorDialogs = preload(
	"res://addons/nexus_resonance/editor/resonance_editor_dialogs.gd"
)

enum ToolMenuId {
	EXPORT_ACTIVE_SCENE,
	EXPORT_ALL_OPEN_SCENES,
	EXPORT_ALL_SCENES_IN_BUILD,
	EXPORT_ACTIVE_SCENE_OBJ,
	EXPORT_ALL_SCENES_OBJ,
	EXPORT_DYNAMIC_OBJECTS_ACTIVE,
	EXPORT_DYNAMIC_OBJECTS_IN_BUILD,
	EXPORT_DYNAMIC_OBJECTS_IN_PROJECT,
	BAKE_ALL_PROBE_VOLUMES,
	CLEAR_PROBE_BATCHES,
	CLEAR_UNREFERENCED_PROBE_DATA,
	UNLINK_PROBE_VOLUME_REFS,
}

var gizmo_instance: EditorNode3DGizmoPlugin = null
var resonance_geometry_inspector: EditorInspectorPlugin = null
var resonance_fmod_event_emitter_inspector: EditorInspectorPlugin = null
var resonance_probe_volume_inspector: EditorInspectorPlugin = null
var bake_runner = null  # ResonanceBakeRunner
var sofa_importer: EditorImportPlugin = null
var probe_data_saver: ResourceFormatSaver = null
var probe_data_loader: ResourceFormatLoader = null
var export_handler = null  # ResonanceExportHandler
var steam_audio_export_plugin: EditorExportPlugin = null
const _tool_submenu_name := "Nexus Resonance"
var _tool_submenu: PopupMenu = null

## Project settings root: Nexus → Resonance (bundles with other Nexus addons under [nexus]).
const SETTINGS_PREFIX := "nexus/resonance/"
const LEGACY_SETTINGS_PREFIX := "audio/nexus_resonance/"


func _enter_tree() -> void:
	_migrate_legacy_project_settings()
	_migrate_nexus_bake_output_dir()
	_clear_obsolete_resonance_project_settings()
	_clear_bus_project_settings()
	_register_logger_project_settings()
	_register_bake_project_settings()
	_init_editor_plugin_ui()


func _migrate_legacy_project_settings() -> void:
	var keys: Array[String] = [
		"logger/categories_enabled",
		"logger/output_to_file",
		"logger/output_to_debug",
		"logger/file_path",
		"logger/steam_audio_verbose",
		"bake_num_rays",
		"bake_num_bounces",
		"bake_num_threads",
		"bake_reflection_type",
		"bake_pathing_num_samples",
		"bake_pathing_vis_range",
		"bake_pathing_path_range",
		"bake_pathing_radius",
		"bake_pathing_threshold",
	]
	for k in keys:
		var old_key := LEGACY_SETTINGS_PREFIX + k
		var new_key := SETTINGS_PREFIX + k
		if not ProjectSettings.has_setting(old_key):
			continue
		if not ProjectSettings.has_setting(new_key):
			ProjectSettings.set_setting(new_key, ProjectSettings.get_setting(old_key))
		ProjectSettings.clear(old_key)

	# Legacy bake/output_dir -> nexus/.../bake/default_output_directory (not the old bus keys).
	var legacy_bake := LEGACY_SETTINGS_PREFIX + "bake/output_dir"
	var new_bake := SETTINGS_PREFIX + "bake/default_output_directory"
	if ProjectSettings.has_setting(legacy_bake) and not ProjectSettings.has_setting(new_bake):
		ProjectSettings.set_setting(new_bake, ProjectSettings.get_setting(legacy_bake))
	if ProjectSettings.has_setting(legacy_bake):
		ProjectSettings.clear(legacy_bake)


func _clear_obsolete_resonance_project_settings() -> void:
	var amb_order_key := SETTINGS_PREFIX + "bake_ambisonics_order"
	if ProjectSettings.has_setting(amb_order_key):
		ProjectSettings.clear(amb_order_key)


func _migrate_nexus_bake_output_dir() -> void:
	var old_k := SETTINGS_PREFIX + "bake/output_dir"
	var new_k := SETTINGS_PREFIX + "bake/default_output_directory"
	if ProjectSettings.has_setting(old_k) and not ProjectSettings.has_setting(new_k):
		ProjectSettings.set_setting(new_k, ProjectSettings.get_setting(old_k))
	if ProjectSettings.has_setting(old_k):
		ProjectSettings.clear(old_k)


func _clear_bus_project_settings() -> void:
	for k in [SETTINGS_PREFIX + "bus", SETTINGS_PREFIX + "reverb_bus_name"]:
		if ProjectSettings.has_setting(k):
			ProjectSettings.clear(k)
	for k in [LEGACY_SETTINGS_PREFIX + "bus", LEGACY_SETTINGS_PREFIX + "reverb_bus_name"]:
		if ProjectSettings.has_setting(k):
			ProjectSettings.clear(k)


func _init_editor_plugin_ui() -> void:
	var saver_script: Script = load(PROBE_DATA_SAVER_SCRIPT) as Script
	if saver_script:
		var saver: ResourceFormatSaver = saver_script.new()
		if saver:
			ResourceSaver.add_resource_format_saver(saver, true)
			probe_data_saver = saver

	var loader_script: Script = load(PROBE_DATA_LOADER_SCRIPT) as Script
	if loader_script:
		var loader: ResourceFormatLoader = loader_script.new()
		if loader:
			ResourceLoader.add_resource_format_loader(loader, true)
			probe_data_loader = loader

	var export_handler_script: Script = load(RESONANCE_EXPORT_HANDLER_SCRIPT) as Script
	if export_handler_script:
		export_handler = export_handler_script.new(get_editor_interface())
	else:
		push_warning(
			"Nexus Resonance: Failed to load export handler. Export and bake features may be unavailable."
		)
	# bake_runner depends on export_handler for export_static_callback (pre-bake static scene export)
	var runner_script: Script = load(RESONANCE_BAKE_RUNNER_SCRIPT) as Script
	if runner_script and export_handler:
		bake_runner = runner_script.new(get_editor_interface())
		bake_runner.export_static_callback = export_handler.export_active_scene

	var pv_inspector_script: Script = load(RESONANCE_PROBE_VOLUME_INSPECTOR_SCRIPT) as Script
	if pv_inspector_script and bake_runner:
		resonance_probe_volume_inspector = pv_inspector_script.new()
		resonance_probe_volume_inspector.bake_runner = bake_runner
		resonance_probe_volume_inspector.editor_interface = get_editor_interface()
		add_inspector_plugin(resonance_probe_volume_inspector)

	if FileAccess.file_exists(GIZMO_SCRIPT):
		var script: Script = load(GIZMO_SCRIPT) as Script
		if script:
			var inst: EditorNode3DGizmoPlugin = script.new()
			if inst:
				gizmo_instance = inst
				if "undo_redo" in gizmo_instance:
					gizmo_instance.undo_redo = get_undo_redo()
				var base: Control = get_editor_interface().get_base_control()
				if base and "fallback_icon" in gizmo_instance:
					gizmo_instance.fallback_icon = ResonanceEditorDialogs.get_icon(
						base, UIStrings.ICON_PROBE_VOLUME_GIZMO, "ReflectionProbe"
					)
				add_node_3d_gizmo_plugin(gizmo_instance)

	_tool_submenu = PopupMenu.new()
	var base: Control = get_editor_interface().get_base_control()
	var icon_export: Texture2D = ResonanceEditorDialogs.get_icon(
		base, UIStrings.ICON_EXPORT, "Export"
	)
	var icon_bake: Texture2D = ResonanceEditorDialogs.get_icon(base, UIStrings.ICON_BAKE, "Bake")
	var icon_clear: Texture2D = ResonanceEditorDialogs.get_icon(base, UIStrings.ICON_CLEAR, "Clear")
	_tool_submenu.add_icon_item(
		icon_export, tr(UIStrings.MENU_EXPORT_ACTIVE_SCENE), ToolMenuId.EXPORT_ACTIVE_SCENE
	)
	_tool_submenu.add_icon_item(
		icon_export, tr(UIStrings.MENU_EXPORT_ALL_OPEN_SCENES), ToolMenuId.EXPORT_ALL_OPEN_SCENES
	)
	_tool_submenu.add_icon_item(
		icon_export,
		tr(UIStrings.MENU_EXPORT_ALL_SCENES_IN_BUILD),
		ToolMenuId.EXPORT_ALL_SCENES_IN_BUILD
	)
	_tool_submenu.add_icon_item(
		icon_export, tr(UIStrings.MENU_EXPORT_ACTIVE_SCENE_OBJ), ToolMenuId.EXPORT_ACTIVE_SCENE_OBJ
	)
	_tool_submenu.add_icon_item(
		icon_export, tr(UIStrings.MENU_EXPORT_ALL_SCENES_OBJ), ToolMenuId.EXPORT_ALL_SCENES_OBJ
	)
	_tool_submenu.add_icon_item(
		icon_export,
		tr(UIStrings.MENU_EXPORT_DYNAMIC_OBJECTS_ACTIVE),
		ToolMenuId.EXPORT_DYNAMIC_OBJECTS_ACTIVE
	)
	_tool_submenu.add_icon_item(
		icon_export,
		tr(UIStrings.MENU_EXPORT_DYNAMIC_OBJECTS_IN_BUILD),
		ToolMenuId.EXPORT_DYNAMIC_OBJECTS_IN_BUILD
	)
	_tool_submenu.add_icon_item(
		icon_export,
		tr(UIStrings.MENU_EXPORT_DYNAMIC_OBJECTS_IN_PROJECT),
		ToolMenuId.EXPORT_DYNAMIC_OBJECTS_IN_PROJECT
	)
	_tool_submenu.add_icon_item(
		icon_bake, tr(UIStrings.MENU_BAKE_ALL_PROBE_VOLUMES), ToolMenuId.BAKE_ALL_PROBE_VOLUMES
	)
	_tool_submenu.add_icon_item(
		icon_clear, tr(UIStrings.MENU_CLEAR_PROBE_BATCHES), ToolMenuId.CLEAR_PROBE_BATCHES
	)
	_tool_submenu.add_icon_item(
		icon_clear,
		tr(UIStrings.MENU_CLEAR_UNREFERENCED_PROBE_DATA),
		ToolMenuId.CLEAR_UNREFERENCED_PROBE_DATA
	)
	_tool_submenu.add_item(
		tr(UIStrings.MENU_UNLINK_PROBE_VOLUME_REFS), ToolMenuId.UNLINK_PROBE_VOLUME_REFS
	)
	_tool_submenu.id_pressed.connect(_on_tool_submenu_id_pressed)
	_register_tool_shortcuts()
	add_tool_submenu_item(_tool_submenu_name, _tool_submenu)

	var importer_script: Script = load(SOFA_IMPORTER_SCRIPT) as Script
	if importer_script:
		sofa_importer = importer_script.new()
		if sofa_importer:
			add_import_plugin(sofa_importer)

	var inspector_script: Script = load(RESONANCE_GEOMETRY_INSPECTOR_SCRIPT) as Script
	if inspector_script:
		resonance_geometry_inspector = inspector_script.new()
		resonance_geometry_inspector.editor_interface = get_editor_interface()
		add_inspector_plugin(resonance_geometry_inspector)

	var fmod_inspector_script: Script = (
		load(RESONANCE_FMOD_EVENT_EMITTER_INSPECTOR_SCRIPT) as Script
	)
	if fmod_inspector_script:
		resonance_fmod_event_emitter_inspector = fmod_inspector_script.new()
		add_inspector_plugin(resonance_fmod_event_emitter_inspector)

	var export_plugin_script: Script = load(RESONANCE_EXPORT_PLUGIN_SCRIPT) as Script
	if export_plugin_script:
		steam_audio_export_plugin = export_plugin_script.new()
		add_export_plugin(steam_audio_export_plugin)

	print_rich("[color=green]Nexus Resonance: Ready.[/color]")


func _exit_tree() -> void:
	_detach_reverb_effect()
	# Do NOT call ResourceSaver.remove_resource_format_saver/ResourceLoader.remove_resource_format_loader:
	# On editor exit Godot may tear these down before _exit_tree; calling remove can trigger SIGSEGV.
	probe_data_saver = null
	probe_data_loader = null
	if resonance_geometry_inspector:
		remove_inspector_plugin(resonance_geometry_inspector)
		resonance_geometry_inspector = null
	if resonance_fmod_event_emitter_inspector:
		remove_inspector_plugin(resonance_fmod_event_emitter_inspector)
		resonance_fmod_event_emitter_inspector = null
	if resonance_probe_volume_inspector:
		remove_inspector_plugin(resonance_probe_volume_inspector)
		resonance_probe_volume_inspector = null
	bake_runner = null
	export_handler = null
	remove_tool_menu_item(_tool_submenu_name)
	_tool_submenu = null
	if steam_audio_export_plugin:
		remove_export_plugin(steam_audio_export_plugin)
		steam_audio_export_plugin = null
	if sofa_importer:
		remove_import_plugin(sofa_importer)
		sofa_importer = null
	if gizmo_instance:
		remove_node_3d_gizmo_plugin(gizmo_instance)


func _enable_plugin() -> void:
	add_autoload_singleton("ResonanceLogger", LOGGER_AUTOLOAD_PATH)
	Callable(self, "_setup_audio_bus").call_deferred()


func _disable_plugin() -> void:
	if Engine.has_singleton("ResonanceServer"):
		var srv: Variant = Engine.get_singleton("ResonanceServer")
		if srv and srv.has_method("clear_probe_batches"):
			srv.clear_probe_batches()
	remove_autoload_singleton("ResonanceLogger")


func _get_bus_editor() -> StringName:
	# Matches ResonanceRuntimeConfig defaults (buses are not Project Settings anymore).
	return &"Master"


func _get_reverb_bus_name_editor() -> StringName:
	return BUS_NAME


func _detach_reverb_effect() -> void:
	var bus_name: StringName = _get_reverb_bus_name_editor()
	var idx: int = AudioServer.get_bus_index(bus_name)
	if idx < 0:
		return
	var n: int = AudioServer.get_bus_effect_count(idx)
	for i in range(n - 1, -1, -1):
		var eff: AudioEffect = AudioServer.get_bus_effect(idx, i)
		if eff and eff.get_class() == EFFECT_CLASS:
			AudioServer.remove_bus_effect(idx, i)
			break


func _setup_audio_bus() -> void:
	var bus_name: StringName = _get_reverb_bus_name_editor()
	var send_name: StringName = _get_bus_editor()
	var idx: int = AudioServer.get_bus_index(bus_name)
	if idx == -1:
		AudioServer.add_bus()
		idx = AudioServer.bus_count - 1
		AudioServer.set_bus_name(idx, bus_name)
		if idx > 1:
			AudioServer.move_bus(idx, 1)
			idx = 1
		if ClassDB.class_exists(EFFECT_CLASS):
			var effect: AudioEffect = ClassDB.instantiate(EFFECT_CLASS)
			effect.resource_name = "Resonance Reverb"
			AudioServer.add_bus_effect(idx, effect)
	if idx >= 0:
		AudioServer.set_bus_send(idx, send_name)


func _has_main_screen() -> bool:
	return false


func _get_plugin_name() -> String:
	return "Resonance"


func _get_plugin_icon() -> Texture2D:
	return get_editor_interface().get_base_control().get_theme_icon(
		"AudioStreamPlayer", "EditorIcons"
	)


func _register_logger_project_settings() -> void:
	const PREFIX := SETTINGS_PREFIX + "logger/"
	if not ProjectSettings.has_setting(PREFIX + "categories_enabled"):
		ProjectSettings.set_setting(
			PREFIX + "categories_enabled", ResonanceLoggerScript.get_default_categories_enabled_dict()
		)
	else:
		var ce: Variant = ProjectSettings.get_setting(PREFIX + "categories_enabled")
		if ce is Dictionary and (ce as Dictionary).is_empty():
			ProjectSettings.set_setting(
				PREFIX + "categories_enabled", ResonanceLoggerScript.get_default_categories_enabled_dict()
			)
	ProjectSettings.add_property_info(
		{
			"name": PREFIX + "categories_enabled",
			"type": TYPE_DICTIONARY,
			"hint": PROPERTY_HINT_NONE,
		}
	)
	if not ProjectSettings.has_setting(PREFIX + "output_to_debug"):
		ProjectSettings.set_setting(PREFIX + "output_to_debug", true)
	ProjectSettings.add_property_info(
		{
			"name": PREFIX + "output_to_debug",
			"type": TYPE_BOOL,
			"hint": PROPERTY_HINT_NONE,
		}
	)
	if not ProjectSettings.has_setting(PREFIX + "output_to_file"):
		ProjectSettings.set_setting(PREFIX + "output_to_file", false)
	ProjectSettings.add_property_info(
		{
			"name": PREFIX + "output_to_file",
			"type": TYPE_BOOL,
			"hint": PROPERTY_HINT_NONE,
		}
	)
	if not ProjectSettings.has_setting(PREFIX + "file_path"):
		ProjectSettings.set_setting(PREFIX + "file_path", "user://nexus_resonance_log.ndjson")
	ProjectSettings.add_property_info(
		{
			"name": PREFIX + "file_path",
			"type": TYPE_STRING,
			"hint": PROPERTY_HINT_NONE,
		}
	)
	if not ProjectSettings.has_setting(PREFIX + "steam_audio_verbose"):
		ProjectSettings.set_setting(PREFIX + "steam_audio_verbose", false)
	ProjectSettings.add_property_info(
		{
			"name": PREFIX + "steam_audio_verbose",
			"type": TYPE_BOOL,
			"hint": PROPERTY_HINT_NONE,
		}
	)


func _register_bake_project_settings() -> void:
	const BAKE_PREFIX := SETTINGS_PREFIX + "bake/"
	const KEY := "default_output_directory"
	if not ProjectSettings.has_setting(BAKE_PREFIX + KEY):
		ProjectSettings.set_setting(BAKE_PREFIX + KEY, "res://audio_data/")
	ProjectSettings.add_property_info(
		{
			"name": BAKE_PREFIX + KEY,
			"type": TYPE_STRING,
			"hint": PROPERTY_HINT_DIR,
		}
	)


func _register_tool_shortcuts() -> void:
	var sc_static: Shortcut = Shortcut.new()
	var ev_static: InputEventKey = InputEventKey.new()
	ev_static.keycode = KEY_E
	ev_static.ctrl_pressed = true
	ev_static.shift_pressed = true
	ev_static.command_or_control_autoremap = true
	sc_static.events = [ev_static]
	_tool_submenu.set_item_shortcut(ToolMenuId.EXPORT_ACTIVE_SCENE, sc_static, true)

	var sc_dynamic: Shortcut = Shortcut.new()
	var ev_dynamic: InputEventKey = InputEventKey.new()
	ev_dynamic.keycode = KEY_D
	ev_dynamic.ctrl_pressed = true
	ev_dynamic.shift_pressed = true
	ev_dynamic.command_or_control_autoremap = true
	sc_dynamic.events = [ev_dynamic]
	_tool_submenu.set_item_shortcut(ToolMenuId.EXPORT_DYNAMIC_OBJECTS_ACTIVE, sc_dynamic, true)

	var sc_bake: Shortcut = Shortcut.new()
	var ev_bake: InputEventKey = InputEventKey.new()
	ev_bake.keycode = KEY_B
	ev_bake.ctrl_pressed = true
	ev_bake.shift_pressed = true
	ev_bake.command_or_control_autoremap = true
	sc_bake.events = [ev_bake]
	_tool_submenu.set_item_shortcut(ToolMenuId.BAKE_ALL_PROBE_VOLUMES, sc_bake, true)


func _on_tool_submenu_id_pressed(id: int) -> void:
	match id:
		ToolMenuId.EXPORT_ACTIVE_SCENE:
			if export_handler:
				export_handler.export_active_scene(null)
		ToolMenuId.EXPORT_ALL_OPEN_SCENES:
			if export_handler:
				export_handler.export_all_open_scenes(null)
		ToolMenuId.EXPORT_ALL_SCENES_IN_BUILD:
			if export_handler:
				export_handler.export_all_scenes_in_build(null)
		ToolMenuId.EXPORT_ACTIVE_SCENE_OBJ:
			if export_handler:
				export_handler.export_scene_obj(null)
		ToolMenuId.EXPORT_ALL_SCENES_OBJ:
			if export_handler:
				export_handler.export_all_scenes_obj(null)
		ToolMenuId.EXPORT_DYNAMIC_OBJECTS_ACTIVE:
			if export_handler:
				export_handler.export_dynamic_mesh(null)
		ToolMenuId.EXPORT_DYNAMIC_OBJECTS_IN_BUILD:
			if export_handler:
				export_handler.export_dynamic_objects_in_build(null)
		ToolMenuId.EXPORT_DYNAMIC_OBJECTS_IN_PROJECT:
			if export_handler:
				export_handler.export_dynamic_objects_in_project(null)
		ToolMenuId.BAKE_ALL_PROBE_VOLUMES:
			_on_tool_bake_all_probe_volumes(null)
		ToolMenuId.CLEAR_PROBE_BATCHES:
			_on_tool_clear_probe_batches(null)
		ToolMenuId.CLEAR_UNREFERENCED_PROBE_DATA:
			if export_handler:
				export_handler.clear_unreferenced_probe_data(null)
		ToolMenuId.UNLINK_PROBE_VOLUME_REFS:
			_on_tool_unlink_probe_refs(null)


func _on_tool_bake_all_probe_volumes(_unused: Variant = null) -> void:
	var root: Node = get_editor_interface().get_edited_scene_root()
	if not root:
		ResonanceEditorDialogs.show_warning(get_editor_interface(), UIStrings.WARN_NO_SCENE)
		return
	var volumes: Array[Node] = []
	ResonanceSceneUtils.collect_resonance_probe_volumes(root, volumes)
	if volumes.is_empty():
		ResonanceEditorDialogs.show_warning(
			get_editor_interface(), tr(UIStrings.WARN_NO_PROBE_VOLUMES)
		)
		return
	if not bake_runner:
		ResonanceEditorDialogs.show_critical(
			get_editor_interface(),
			tr(UIStrings.ERR_BAKE_RUNNER_NOT_INIT),
			tr(UIStrings.DIALOG_BAKE_FAILED_TITLE)
		)
		return
	bake_runner.run_bake(volumes)


func _on_tool_clear_probe_batches(_unused: Variant = null) -> void:
	if Engine.has_singleton("ResonanceServer"):
		var srv: Variant = Engine.get_singleton("ResonanceServer")
		if srv and srv.has_method("clear_probe_batches"):
			srv.clear_probe_batches()
			ResonanceEditorDialogs.show_info(tr(UIStrings.INFO_PROBE_BATCHES_CLEARED))
	else:
		ResonanceEditorDialogs.show_critical(
			get_editor_interface(),
			tr(UIStrings.ERR_GDEXTENSION_NOT_LOADED),
			tr(UIStrings.DIALOG_GDEXTENSION_NOT_LOADED_TITLE)
		)


func _on_tool_unlink_probe_refs(_unused: Variant = null) -> void:
	var root: Node = get_editor_interface().get_edited_scene_root()
	if not root:
		ResonanceEditorDialogs.show_warning(get_editor_interface(), UIStrings.WARN_NO_SCENE)
		return
	var selected_volumes: Array[Node] = []
	for n in get_editor_interface().get_selection().get_selected_nodes():
		if n.is_class("ResonanceProbeVolume"):
			selected_volumes.append(n)
	if selected_volumes.is_empty():
		ResonanceEditorDialogs.show_warning(
			get_editor_interface(), tr(UIStrings.WARN_SELECT_PROBE_VOLUMES)
		)
		return
	var ur: EditorUndoRedoManager = get_undo_redo()
	ur.create_action("Nexus Resonance: Unlink Probe Volume References")
	var result: Dictionary = {"count": 0, "nodes": []}
	_collect_probe_refs_to_clear(root, root, selected_volumes, result)
	if result["count"] == 0:
		ResonanceEditorDialogs.show_warning(
			get_editor_interface(), tr(UIStrings.WARN_NO_PLAYER_REFS)
		)
		return
	var nodes_to_clear: Array[Node] = []
	for n in result["nodes"]:
		nodes_to_clear.append(n as Node)
	for n in nodes_to_clear:
		var old_path: NodePath = n.get_pathing_probe_volume()
		ur.add_do_property(n, "pathing_probe_volume", NodePath())
		ur.add_undo_property(n, "pathing_probe_volume", old_path)
	ur.commit_action()
	ResonanceEditorDialogs.show_success_toast(
		get_editor_interface(), tr(UIStrings.INFO_UNLINK_DONE) % result["count"]
	)


func _collect_probe_refs_to_clear(
	node: Node, scene_root: Node, targets: Array[Node], result: Dictionary
) -> void:
	if node.has_method("get_pathing_probe_volume") and node.has_method("set_pathing_probe_volume"):
		var path: NodePath = node.get_pathing_probe_volume()
		if not path.is_empty():
			var target: Node = scene_root.get_node_or_null(path)
			if target and target in targets:
				result["nodes"].append(node)
				result["count"] += 1
	for c in node.get_children():
		_collect_probe_refs_to_clear(c, scene_root, targets, result)

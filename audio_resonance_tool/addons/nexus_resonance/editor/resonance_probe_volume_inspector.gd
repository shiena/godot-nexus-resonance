@tool
extends EditorInspectorPlugin

## Adds Bake Probes button to ResonanceProbeVolume inspector. Uses shared ResonanceBakeRunner.
## bake_runner must be set by the plugin before add_inspector_plugin.

const UIStrings = preload("res://addons/nexus_resonance/scripts/resonance_ui_strings.gd")
const ResonanceEditorDialogs = preload(
	"res://addons/nexus_resonance/editor/resonance_editor_dialogs.gd"
)

var bake_runner = null  # ResonanceBakeRunner
var editor_interface: EditorInterface = null


func _can_handle(object: Object) -> bool:
	return object != null and object.is_class("ResonanceProbeVolume")


func _parse_begin(object: Object) -> void:
	if not editor_interface:
		return
	if bake_runner:
		var volumes: Array[Node] = []
		volumes.append(object)
		bake_runner.ensure_resonance_server_for_volumes(volumes)

	# Optional info panel with step-by-step guide
	var foldable: FoldableContainer = FoldableContainer.new()
	foldable.title = tr(UIStrings.INSPECTOR_WORKFLOW_GUIDE)
	foldable.folded = true
	var steps: VBoxContainer = VBoxContainer.new()
	steps.add_theme_constant_override("separation", 4)
	var step_lines: Array = [
		UIStrings.INSPECTOR_STEP_1,
		UIStrings.INSPECTOR_STEP_2,
		UIStrings.INSPECTOR_STEP_3,
		UIStrings.INSPECTOR_STEP_4,
		UIStrings.INSPECTOR_STEP_5
	]
	for line in step_lines:
		var lbl = Label.new()
		lbl.text = tr(line)
		lbl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		lbl.add_theme_font_size_override("font_size", 12)
		steps.add_child(lbl)
	foldable.add_child(steps)
	add_custom_control(foldable)

	# Validation prereqs in collapsible section (Ready/Not ready in header)
	var base: Control = editor_interface.get_base_control() if editor_interface else null
	if bake_runner and base and bake_runner.has_method("get_validation_checklist_for_volume"):
		var checklist = bake_runner.get_validation_checklist_for_volume(object)
		var any_missing = false
		for item in checklist:
			if not item.get("ok", false):
				any_missing = true
				break
		var prereq_foldable: FoldableContainer = FoldableContainer.new()
		prereq_foldable.title = (
			tr(UIStrings.INSPECTOR_PREREQ_READY)
			if not any_missing
			else tr(UIStrings.INSPECTOR_PREREQ_NOT_READY)
		)
		prereq_foldable.folded = true
		prereq_foldable.add_theme_color_override(
			"font_color", Color(0.4, 0.9, 0.4) if not any_missing else Color(1.0, 0.4, 0.4)
		)
		var prereq_vbox = VBoxContainer.new()
		prereq_vbox.add_theme_constant_override("separation", 2)
		for item in checklist:
			var ok: bool = item.get("ok", false)
			var label_str: String = str(item.get("label", ""))
			prereq_vbox.add_child(
				ResonanceEditorDialogs.create_checklist_row(base, label_str, ok, 14, 4)
			)
		prereq_foldable.add_child(prereq_vbox)
		add_custom_control(prereq_foldable)

	# Preview Bake Settings
	var preview_btn = Button.new()
	preview_btn.text = tr(UIStrings.BTN_PREVIEW_BAKE_SETTINGS)
	preview_btn.tooltip_text = tr(UIStrings.TT_PREVIEW_BAKE)
	preview_btn.pressed.connect(_on_preview_bake_pressed.bind(object))
	add_custom_control(preview_btn)

	# Bake Probes
	var btn = Button.new()
	btn.text = tr(UIStrings.BTN_BAKE_PROBES)
	btn.tooltip_text = tr(UIStrings.TT_BAKE_PROBES)
	btn.icon = ResonanceEditorDialogs.get_icon(base, UIStrings.ICON_BAKE, "Bake")
	btn.pressed.connect(_on_bake_pressed.bind(object))
	add_custom_control(btn)


func _on_preview_bake_pressed(obj: Object) -> void:
	if not obj or not obj.is_class("ResonanceProbeVolume"):
		return
	var probe_count = (
		bake_runner.estimate_probe_count(obj)
		if bake_runner and bake_runner.has_method("estimate_probe_count")
		else -1
	)
	var est_time = (
		bake_runner.estimate_bake_time(obj)
		if bake_runner and bake_runner.has_method("estimate_bake_time")
		else ""
	)
	var msg := ""
	if probe_count >= 0:
		msg = tr(UIStrings.PROGRESS_PROBES) % probe_count
	if not est_time.is_empty():
		msg = (
			msg + "\n" + tr(UIStrings.PROGRESS_ESTIMATED_TIME) % est_time
			if not msg.is_empty()
			else tr(UIStrings.PROGRESS_ESTIMATED_TIME) % est_time
		)
	if msg.is_empty():
		msg = tr(UIStrings.INFO_CONFIGURE_BAKE_CONFIG_FOR_ESTIMATES)
	if editor_interface:
		ResonanceEditorDialogs.show_success_toast(editor_interface, msg)
	else:
		print_rich("[color=cyan]Nexus Resonance:[/color] " + msg)


func _on_bake_pressed(obj: Object) -> void:
	if not obj or not obj.is_class("ResonanceProbeVolume"):
		return
	if bake_runner:
		var volumes: Array[Node] = []
		volumes.append(obj)
		bake_runner.run_bake(volumes)
	else:
		ResonanceEditorDialogs.show_warning(
			editor_interface, tr(UIStrings.WARN_BAKE_RUNNER_NOT_SET)
		)

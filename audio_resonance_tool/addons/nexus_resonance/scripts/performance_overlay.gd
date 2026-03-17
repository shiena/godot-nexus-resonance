extends CanvasLayer

## Optional performance overlay showing FPS and frame time.
## Independent from the Debug Overlay; toggle with F4 when enabled on ResonanceRuntime.

var _panel: PanelContainer
var _label: RichTextLabel
var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.2
const COLOR_NEUTRAL := "#dddddd"


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_build_ui()
	get_viewport().size_changed.connect(_update_position)


func _build_ui() -> void:
	layer = 101  # Above debug overlay (100)
	_panel = PanelContainer.new()
	var style = StyleBoxFlat.new()
	style.bg_color = Color(0, 0, 0, 0.75)
	style.border_color = Color(0.4, 0.6, 0.9)
	style.set_border_width_all(2)
	style.set_corner_radius_all(4)
	style.set_content_margin_all(8)
	_panel.add_theme_stylebox_override("panel", style)

	_label = RichTextLabel.new()
	_label.bbcode_enabled = true
	_label.fit_content = true
	_label.add_theme_font_size_override("normal_font_size", 14)
	_label.text = "Performance"
	_panel.add_child(_label)
	add_child(_panel)

	_panel.set_anchors_preset(Control.PRESET_TOP_LEFT)
	_panel.custom_minimum_size = Vector2(160, 80)
	_panel.size_flags_horizontal = Control.SIZE_SHRINK_END
	call_deferred("_update_position")


func _update_position() -> void:
	if not _panel:
		return
	var vp = get_viewport()
	if vp:
		var size = vp.get_visible_rect().size
		_panel.position = Vector2(size.x - _panel.custom_minimum_size.x - 20, 10)


func _process(delta: float) -> void:
	if Engine.is_editor_hint():
		return
	if not visible:
		return
	if not _label:
		return

	_update_timer += delta
	if _update_timer >= UPDATE_INTERVAL:
		_update_timer = 0.0
		_refresh()


func _refresh() -> void:
	var fps := Performance.get_monitor(Performance.TIME_FPS)
	var process_ms := Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0
	var physics_ms := Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0
	_label.text = "[color=%s]FPS: %d[/color]\n[color=%s]Frame: %.2f ms[/color]\n[color=%s]Physics: %.2f ms[/color]" % [
		COLOR_NEUTRAL, int(fps),
		COLOR_NEUTRAL, process_ms,
		COLOR_NEUTRAL, physics_ms
	]

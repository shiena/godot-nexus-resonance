extends CanvasLayer

## Optional performance overlay showing FPS, frame time, and Nexus main-thread vs worker tick durations (µs).
## Toggle with the key set on [member ResonanceRuntime.performance_overlay_toggle_key] (default F2) when [member ResonanceRuntime.enable_debug] is on.

var _panel: PanelContainer
var _label: RichTextLabel
var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.2
const COLOR_NEUTRAL := "#dddddd"
const COLOR_HINT := "#88aacc"


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
	_panel.custom_minimum_size = Vector2(320, 140)
	_panel.size_flags_horizontal = Control.SIZE_SHRINK_END
	visible = false
	call_deferred("_update_position")


func _exit_tree() -> void:
	visible = false
	process_mode = Node.PROCESS_MODE_DISABLED


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
	var mtu := 0
	var w_d := 0
	var w_r := 0
	var w_p := 0
	var w_s := 0
	var tree := get_tree()
	if tree:
		var rt: Node = tree.get_first_node_in_group("resonance_runtime")
		if rt:
			var v: Variant = rt.get("main_thread_last_tick_usec")
			mtu = int(v) if v != null else 0
	if Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv and srv.is_initialized() and srv.has_method("get_simulation_worker_timing"):
			var wtim: Dictionary = srv.get_simulation_worker_timing()
			w_d = int(wtim.get("us_run_direct", 0))
			w_r = int(wtim.get("us_run_reflections", 0))
			w_p = int(wtim.get("us_run_pathing", 0))
			w_s = int(wtim.get("us_sync_fetch", 0))
	var w_sum := w_d + w_r + w_p + w_s
	# Use String.format — C-style "%" formatting can miscount vs BBCode / float specifiers in this engine.
	_label.text = (
		"[color={c}]FPS: {fps}[/color]\n[color={c}]Frame: {proc_ms} ms[/color]\n[color={c}]Physics: {phy_ms} ms[/color]\n"
		+ "[color={c}]Nexus main _process:[/color] {mtu} µs\n"
		+ "[color={c}]Worker last (µs):[/color] d={w_d} r={w_r} p={w_p} s={w_s} [color={h}](Σ {w_sum})[/color]\n"
		+ "[color={c}]CPU:[/color] if one core is pegged, check main vs worker vs audio thread."
	).format(
		{
			"c": COLOR_NEUTRAL,
			"h": COLOR_HINT,
			"fps": int(fps),
			"proc_ms": String.num(process_ms, 2),
			"phy_ms": String.num(physics_ms, 2),
			"mtu": mtu,
			"w_d": w_d,
			"w_r": w_r,
			"w_p": w_p,
			"w_s": w_s,
			"w_sum": w_sum,
		}
	)

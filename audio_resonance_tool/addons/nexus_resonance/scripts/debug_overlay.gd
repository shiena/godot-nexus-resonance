extends CanvasLayer

## Debug overlay that displays Nexus Resonance server state, reflection type, realtime rays,
## and a filterable log from ResonanceLogger.
## Per-source occlusion/reverb data appears as 3D labels above each playing ResonancePlayer
## when Debug Occlusion or Debug Reflections is enabled on ResonanceRuntime's runtime.
## NOTE: Reflection ray viz requires Realtime Rays > 0 and debug_reflections in ResonanceRuntimeConfig.

var _panel: PanelContainer
var _vbox: VBoxContainer
var _status_label: RichTextLabel
var _audio_instrumentation_label: RichTextLabel
var _reverb_bus_label: RichTextLabel
var _log_section: VBoxContainer
var _log_category_filters: HBoxContainer
var _log_scroll: ScrollContainer
var _log_label: RichTextLabel
var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.2
const REFLECTION_NAMES: Array[String] = ["Convolution", "Parametric", "Hybrid"]
const COLOR_OK := "#44ff44"
const COLOR_WARNING := "#ffcc44"
const COLOR_ERROR := "#ff6666"
const COLOR_NEUTRAL := "#dddddd"

func _ready():
	if Engine.is_editor_hint():
		return
	_build_ui()

func _build_ui():
	layer = 100
	_panel = PanelContainer.new()
	var style = StyleBoxFlat.new()
	style.bg_color = Color(0, 0, 0, 0.75)
	style.border_color = Color(0.4, 0.6, 0.9)
	style.set_border_width_all(2)
	style.set_corner_radius_all(4)
	style.set_content_margin_all(8)
	_panel.add_theme_stylebox_override("panel", style)

	_vbox = VBoxContainer.new()
	_vbox.add_theme_constant_override("separation", 8)

	var fold_server = FoldableContainer.new()
	fold_server.title = "Server Status"
	fold_server.folded = false
	_status_label = RichTextLabel.new()
	_status_label.bbcode_enabled = true
	_status_label.fit_content = true
	_status_label.add_theme_font_size_override("normal_font_size", 12)
	_status_label.text = "Nexus Resonance Debug"
	fold_server.add_child(_status_label)
	_vbox.add_child(fold_server)

	var fold_audio = FoldableContainer.new()
	fold_audio.title = "Audio Instrumentation (dropout debug)"
	fold_audio.folded = true
	_audio_instrumentation_label = RichTextLabel.new()
	_audio_instrumentation_label.bbcode_enabled = true
	_audio_instrumentation_label.fit_content = true
	_audio_instrumentation_label.add_theme_font_size_override("normal_font_size", 11)
	_audio_instrumentation_label.text = "(No data)"
	fold_audio.add_child(_audio_instrumentation_label)
	_vbox.add_child(fold_audio)

	var fold_reverb = FoldableContainer.new()
	fold_reverb.title = "Reverb Bus (Convolution)"
	fold_reverb.folded = true
	_reverb_bus_label = RichTextLabel.new()
	_reverb_bus_label.bbcode_enabled = true
	_reverb_bus_label.fit_content = true
	_reverb_bus_label.add_theme_font_size_override("normal_font_size", 11)
	_reverb_bus_label.text = "(No data)"
	fold_reverb.add_child(_reverb_bus_label)
	_vbox.add_child(fold_reverb)

	_log_section = VBoxContainer.new()
	_log_section.add_theme_constant_override("separation", 4)
	var fold_log = FoldableContainer.new()
	fold_log.title = "Log (ResonanceLogger)"
	fold_log.folded = true

	_log_category_filters = HBoxContainer.new()
	_log_category_filters.add_theme_constant_override("separation", 4)
	if Engine.has_singleton("ResonanceLogger"):
		var logger = Engine.get_singleton("ResonanceLogger")
		for cat in logger.get_all_categories():
			var cb = CheckBox.new()
			cb.text = String(cat)
			cb.button_pressed = logger.is_category_enabled(cat)
			cb.toggled.connect(_on_category_toggled.bind(cat))
			_log_category_filters.add_child(cb)
	_log_section.add_child(_log_category_filters)

	_log_scroll = ScrollContainer.new()
	_log_scroll.custom_minimum_size = Vector2(360, 120)
	_log_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_log_label = RichTextLabel.new()
	_log_label.bbcode_enabled = true
	_log_label.fit_content = true
	_log_label.scroll_following = true
	_log_label.add_theme_font_size_override("normal_font_size", 10)
	_log_label.text = "(No log entries)"
	_log_scroll.add_child(_log_label)
	_log_section.add_child(_log_scroll)
	fold_log.add_child(_log_section)
	_vbox.add_child(fold_log)
	_panel.add_child(_vbox)
	add_child(_panel)

	_panel.set_anchors_preset(Control.PRESET_TOP_LEFT)
	_panel.position = Vector2(10, 10)
	_panel.custom_minimum_size = Vector2(380, 0)
	_panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN

func _on_category_toggled(pressed: bool, category: StringName):
	if Engine.has_singleton("ResonanceLogger"):
		Engine.get_singleton("ResonanceLogger").set_category_enabled(category, pressed)

func _process(delta: float):
	if Engine.is_editor_hint():
		return
	if not _panel or not _status_label:
		return

	_update_timer += delta
	if _update_timer >= UPDATE_INTERVAL:
		_update_timer = 0.0
		_refresh_status()
		_refresh_audio_instrumentation()
		_refresh_reverb_bus()
		_refresh_log()

func _refresh_status():
	if not Engine.has_singleton("ResonanceServer"):
		_status_label.text = "[color=%s]ResonanceServer not loaded[/color]" % COLOR_ERROR
		return

	var srv = Engine.get_singleton("ResonanceServer")
	var parts: PackedStringArray = []

	var out_direct = srv.is_output_direct_enabled()
	var out_reverb = srv.is_output_reverb_enabled()
	parts.append("[color=%s]Output Direct: %s | Reverb: %s[/color]" % [COLOR_NEUTRAL, _str_bool(out_direct), _str_bool(out_reverb)])

	var refl_type = srv.get_reflection_type() if srv.has_method("get_reflection_type") else 0
	var refl_name = REFLECTION_NAMES[refl_type] if refl_type >= 0 and refl_type < 3 else "?"
	parts.append("[color=%s]Reflection Type: %s[/color]" % [COLOR_NEUTRAL, refl_name])

	if srv.has_method("get_realtime_rays"):
		var rays = srv.get_realtime_rays()
		parts.append("[color=%s]Realtime Rays: %s[/color]" % [COLOR_NEUTRAL, rays if rays > 0 else "Baked Only (0)"])
	if refl_type == 1 or refl_type == 2:
		parts.append("[color=%s]  [Parametric/Hybrid: Reverb in Player output][/color]" % COLOR_WARNING)

	var init_ok = srv.is_initialized()
	var sim_ok = srv.is_simulating() if init_ok else false
	var server_color := COLOR_ERROR
	if init_ok:
		server_color = COLOR_OK if sim_ok else COLOR_WARNING
	parts.append("[color=%s]Server: %s[/color]" % [server_color, "initialized" if init_ok else "not initialized"])
	if init_ok:
		parts.append("[color=%s]Simulation: %s[/color]" % [COLOR_OK if sim_ok else COLOR_WARNING, "running" if sim_ok else "waiting for geometry"])

	_status_label.text = "\n".join(parts)

func _refresh_audio_instrumentation():
	if not _audio_instrumentation_label:
		return
	var tree = get_tree()
	if not tree:
		return
	var players = tree.get_nodes_in_group("resonance_player")
	if players.is_empty():
		_audio_instrumentation_label.text = "[color=%s]No ResonancePlayer nodes[/color]" % COLOR_WARNING
		return
	var block_size := -1
	if Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv and srv.has_method("get_audio_frame_size"):
			block_size = srv.get_audio_frame_size()
	var parts: PackedStringArray = []
	for p in players:
		if not p.has_method("get_audio_instrumentation"):
			continue
		var inst = p.get_audio_instrumentation()
		if inst.is_empty():
			parts.append("[color=%s]%s: (not playing)[/color]" % [COLOR_NEUTRAL, p.name])
			continue
		var input_dropped = inst.get("input_dropped", 0)
		var output_underrun = inst.get("output_underrun", 0)
		var output_blocked = inst.get("output_blocked", 0)
		var mix_calls = inst.get("mix_calls", 0)
		var blocks = inst.get("blocks_processed", 0)
		var passthrough = inst.get("passthrough_blocks", 0)
		var reverb_miss = inst.get("reverb_miss_blocks", 0)
		var max_block_us = inst.get("max_block_time_us", 0)
		var late_mix = inst.get("late_mix_count", 0)
		var param_syncs = inst.get("param_sync_count", 0)
		var zero_input = inst.get("zero_input_count", 0)
		var silent = inst.get("silent_output_blocks", 0)
		var last_rms = inst.get("last_output_rms", 0.0)
		var buf_ok = (input_dropped == 0 and output_underrun == 0 and output_blocked == 0)
		var timing_ok = (late_mix == 0)
		var status_ok = buf_ok and timing_ok
		var status_color := COLOR_OK if status_ok else (COLOR_WARNING if not buf_ok or late_mix < 3 else COLOR_ERROR)
		var max_ms = "%.1f" % (max_block_us / 1000.0)
		var proc_parts: PackedStringArray = []
		if block_size > 0:
			proc_parts.append("block=%d" % block_size)
		proc_parts.append("zero_in=%d rms=%.4f" % [zero_input, last_rms])
		var proc_extras = " ".join(proc_parts)
		parts.append("[color=%s]%s:[/color]" % [COLOR_NEUTRAL, p.name])
		parts.append("  buf: drop=%d underrun=%d blocked=%d | late=%d max_block=%sms psync=%d" % [input_dropped, output_underrun, output_blocked, late_mix, max_ms, param_syncs])
		parts.append("  proc: pass=%d rmiss=%d silent=%d | %s [color=%s][%s][/color]" % [passthrough, reverb_miss, silent, proc_extras, status_color, "OK" if status_ok else "CHECK"])
	_audio_instrumentation_label.text = "\n".join(parts)

func _refresh_reverb_bus():
	if not _reverb_bus_label:
		return
	var srv = Engine.get_singleton("ResonanceServer") if Engine.has_singleton("ResonanceServer") else null
	var parts: PackedStringArray = []
	if not srv or not srv.has_method("get_reverb_bus_instrumentation"):
		_reverb_bus_label.text = "[color=%s]ResonanceServer not available[/color]" % COLOR_ERROR
		return
	var ri = srv.get_reverb_bus_instrumentation()
	var refl_type = ri.get("reflection_type", -1)
	var refl_names: Array[String] = ["Convolution", "Parametric", "Hybrid"]
	var refl_name = refl_names[refl_type] if refl_type >= 0 and refl_type < 3 else "?"
	var mixer_ok = ri.get("mixer_exists", false)
	parts.append("[color=%s]Reflection Type: %s (Convolution=0 uses Reverb Bus)[/color]" % [COLOR_NEUTRAL, refl_name])
	parts.append("[color=%s]Mixer: exists=%s | feeds=%d[/color]" % [COLOR_OK if mixer_ok else COLOR_WARNING, ri.get("mixer_exists", false), ri.get("mixer_feed_count", 0)])
	if refl_type == 0:
		parts.append("Convolution: valid_fetches=%d feed_ir_null=%d" % [
			ri.get("convolution_valid_fetches", 0),
			ri.get("convolution_feed_ir_null", 0),
		])
		parts.append("  gain_min=%.6f gain_max=%.6f input_rms_max=%.6f" % [
			ri.get("convolution_gain_min", 1.0),
			ri.get("convolution_gain_max", 0.0),
			ri.get("convolution_input_rms_max", 0.0),
		])
	var eff_success = ri.get("effect_success", 0)
	var eff_color := COLOR_OK if eff_success > 0 else COLOR_WARNING
	parts.append("[color=%s]Effect: process=%d mixer_null=%d success=%d frames=%d peak=%.6f[/color]" % [
		eff_color,
		ri.get("effect_process_calls", 0),
		ri.get("effect_mixer_null", 0),
		eff_success,
		ri.get("effect_frames_written", 0),
		ri.get("effect_output_peak", 0.0),
	])
	var runtimes = get_tree().get_nodes_in_group("resonance_runtime")
	if not runtimes.is_empty():
		var rt = runtimes[0]
		var ai = rt.get("activator_instrumentation")
		if ai != null:
			if ai.is_empty():
				parts.append("Activator: (no data yet)")
			else:
				var act = ai.get("active", false)
				var reason = ai.get("reason", "")
				if act:
					parts.append("Activator: active | frames=%d calls=%d bus_idx=%d muted=%s send=%s skips=%s" % [
						ai.get("frames_pushed_total", 0),
						ai.get("fill_calls", 0),
						ai.get("bus_index", -1),
						ai.get("bus_muted", true),
						ai.get("bus_send", ""),
						ai.get("skips", "?"),
					])
				else:
					parts.append("Activator: inactive | reason=%s" % reason)
	_reverb_bus_label.text = "Reverb Bus (Convolution):\n" + "\n".join(parts)

func _refresh_log():
	if not _log_label or not Engine.has_singleton("ResonanceLogger"):
		return
	var logger = Engine.get_singleton("ResonanceLogger")
	var entries = logger.get_recent_entries(32)
	if entries.is_empty():
		_log_label.text = "[color=%s](No log entries)[/color]" % COLOR_NEUTRAL
		return
	var parts: PackedStringArray = []
	for i in range(entries.size() - 1, -1, -1):
		var e = entries[i]
		var ts = e.get("timestamp", 0)
		var cat = e.get("category", "")
		var msg = e.get("message", "")
		var data = e.get("data", {})
		var is_error = data.get("error", false) or msg.to_lower().contains("error") or msg.to_lower().contains("failed")
		var line_color := COLOR_ERROR if is_error else (COLOR_WARNING if msg.to_lower().contains("warn") else COLOR_NEUTRAL)
		parts.append("[color=%s][%s] %s: %s[/color]" % [line_color, ts, cat, msg])
	_log_label.text = "\n".join(parts)
	if _log_scroll and _log_label:
		_log_scroll.scroll_vertical = int(_log_label.size.y)

func _str_bool(b: bool) -> String:
	return "ON" if b else "OFF"

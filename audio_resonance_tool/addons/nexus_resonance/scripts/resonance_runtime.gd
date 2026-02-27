@tool
extends Node
class_name ResonanceRuntime

## Nexus Resonance scene configuration node.
## Add via Add Child Node > ResonanceRuntime.
## Assign a ResonanceRuntimeConfig resource (create new or link existing) for runtime settings.

const ResonanceRuntimeConfig = preload("res://addons/nexus_resonance/scripts/resonance_runtime_config.gd")
const ResonanceBakeConfig = preload("res://addons/nexus_resonance/scripts/resonance_bake_config.gd")
const ResonanceSceneUtils = preload("res://addons/nexus_resonance/scripts/resonance_scene_utils.gd")
const REVERB_BUS_NAME = "ResonanceReverb"

var _runtime: ResonanceRuntimeConfig
## Runtime configuration resource. Create or link .tres. Auto-created default if empty.
@export var runtime: ResonanceRuntimeConfig:
	get:
		return _runtime
	set(val):
		_disconnect_runtime_signals()
		_runtime = val
		_connect_runtime_signals()
		_warn_restart_if_needed()

@export_group("Runtime Debug")
## Show debug overlay (audio frame size, etc.) in game.
@export var debug_overlay_visible: bool = false
## Draw source rays for debugging. Requires ResonanceServer debug support.
@export var debug_sources: bool = false

var _activator: AudioStreamPlayer

## Reverb Bus activator instrumentation (for debugging convolution routing).
## Read from debug overlay via get_tree().get_first_node_in_group("resonance_runtime").
var activator_instrumentation: Dictionary = {}
var _activator_frames_pushed: int = 0
var _activator_fill_calls: int = 0

## Returns bake params from first Probe Volume with bake_config, or default. Used before init so pathing visibility params are set.
func _get_bake_params_for_runtime() -> Dictionary:
	var tree = get_tree()
	if not tree:
		return ResonanceBakeConfig.create_default().get_bake_params()
	var volumes = tree.get_nodes_in_group("resonance_probe_volume")
	for vol in volumes:
		var bc = vol.get("bake_config") if "bake_config" in vol else null
		if bc and bc.has_method("get_bake_params"):
			return bc.get_bake_params()
	return ResonanceBakeConfig.create_default().get_bake_params()

## Returns config dict for init_audio_engine. Merges runtime config with node's debug flags.
func get_config_dict() -> Dictionary:
	var cfg: Dictionary = _runtime.get_config() if _runtime else {}
	cfg["debug_occlusion"] = debug_sources
	return cfg

func _ready():
	if not _runtime:
		_runtime = ResonanceRuntimeConfig.create_default()
	add_to_group("resonance_runtime")
	set_process_priority(100)
	_connect_runtime_signals()
	if Engine.is_editor_hint():
		call_deferred("_notify_volumes_runtime_config_changed")
	if not get_node_or_null("DebugOverlay"):
		var overlay_script = load("res://addons/nexus_resonance/scripts/debug_overlay.gd") as GDScript
		if overlay_script:
			var overlay = overlay_script.new()
			overlay.name = "DebugOverlay"
			add_child(overlay)
	_initialize_server()
	if not Engine.is_editor_hint():
		_setup_activator()
	_update_debug_overlay_visibility()

func _process(delta: float):
	if Engine.is_editor_hint():
		return
	_fill_activator_buffer()
	if Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv.is_initialized():
			var vp = get_viewport()
			if vp:
				var cam = vp.get_camera_3d()
				if cam:
					var listeners = get_tree().get_nodes_in_group("resonance_listener")
					if listeners.is_empty():
						srv.update_listener(cam.global_position, -cam.global_transform.basis.z, cam.global_transform.basis.y)
			srv.tick(delta)

func _initialize_server():
	# In editor, probe_toolbar inits when needed for bake. Avoid Steam Audio init on scene load (can crash).
	if Engine.is_editor_hint():
		return
	if not Engine.has_singleton("ResonanceServer"):
		push_error("Nexus Resonance: GDExtension not loaded.")
		return

	var cfg = get_config_dict()
	if cfg.is_empty():
		push_error("Nexus Resonance: Failed to build config.")
		return

	var srv = Engine.get_singleton("ResonanceServer")
	if srv.is_initialized() and srv.has_method("reinit_audio_engine"):
		_prepare_geometry_before_reinit()
		srv.reinit_audio_engine(cfg)
		call_deferred("_reload_after_reinit")
		return
	if srv.is_initialized():
		return
	# Set bake params before init so pathing visibility params (from bake_config) are available.
	var bake_params = _get_bake_params_for_runtime()
	srv.set_bake_params(bake_params)
	srv.init_audio_engine(cfg)
	# Trigger geometry refresh for debug reflection viz (ResonanceGeometry re-registers after init)
	if srv.wants_debug_reflection_viz():
		get_tree().call_group_flags(SceneTree.GROUP_CALL_DEFERRED, "resonance_geometry", "refresh_geometry")
	# Unity-style: load static scene(s) from ResonanceStaticScene nodes. Additive: one per scene.
	var static_scenes: Array[Node] = []
	ResonanceSceneUtils.collect_resonance_static_scenes(get_tree().get_root(), static_scenes)
	if not static_scenes.is_empty():
		if srv.has_method("clear_static_scenes") and srv.has_method("add_static_scene_from_asset"):
			srv.clear_static_scenes()
			for ss in static_scenes:
				if ss.static_scene_asset and ss.has_valid_asset():
					srv.add_static_scene_from_asset(ss.static_scene_asset)
		elif srv.has_method("load_static_scene_from_asset") and static_scenes.size() == 1:
			if static_scenes[0].static_scene_asset and static_scenes[0].has_valid_asset():
				srv.load_static_scene_from_asset(static_scenes[0].static_scene_asset)
	_apply_debug_flags()
	_apply_perspective_correction()

func _apply_debug_flags():
	if not Engine.has_singleton("ResonanceServer"):
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if srv.is_initialized():
		srv.set_debug_occlusion(debug_sources)
		if srv.has_method("set_debug_reflections"):
			srv.set_debug_reflections(_runtime.debug_reflections)
		if srv.has_method("set_debug_pathing"):
			srv.set_debug_pathing(_runtime.debug_pathing)

func _apply_perspective_correction():
	if not Engine.has_singleton("ResonanceServer"):
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if srv.is_initialized():
		srv.set_perspective_correction_enabled(runtime.perspective_correction_enabled)
		srv.set_perspective_correction_factor(runtime.perspective_correction_factor)
		srv.set_reverb_transmission_amount(runtime.reverb_transmission_amount)

func _setup_activator():
	if AudioServer.get_bus_index(REVERB_BUS_NAME) == -1:
		return
	_activator = AudioStreamPlayer.new()
	_activator.name = "ResonanceInternalActivator"
	_activator.bus = REVERB_BUS_NAME
	var gen = AudioStreamGenerator.new()
	gen.buffer_length = 0.1
	_activator.stream = gen
	add_child(_activator)
	_activator.play()
	# Ensure Reverb Bus sends to Master (critical for convolution reverb to reach speakers)
	var idx = AudioServer.get_bus_index(REVERB_BUS_NAME)
	if idx >= 0 and AudioServer.get_bus_send(idx) != &"Master":
		AudioServer.set_bus_send(idx, &"Master")


func _fill_activator_buffer():
	## Keeps the Reverb Bus active so Godot does not disable it after silence.
	## The activator feeds a low-level signal so the bus stays active; the effect
	## overwrites output with mixer content.
	if not _activator or not _activator.playing:
		activator_instrumentation = {"active": false, "reason": "no_activator_or_not_playing"}
		return
	var playback = _activator.get_stream_playback()
	if not playback:
		activator_instrumentation = {"active": false, "reason": "no_playback"}
		return
	if not playback is AudioStreamGeneratorPlayback:
		activator_instrumentation = {"active": false, "reason": "not_generator"}
		return
	var avail = playback.get_frames_available()
	if avail <= 0:
		activator_instrumentation["active"] = true
		activator_instrumentation["avail_zero_count"] = activator_instrumentation.get("avail_zero_count", 0) + 1
		return
	const AMP := 1e-5
	var to_push = min(avail, 512)
	for i in to_push:
		playback.push_frame(Vector2(AMP, AMP))
	_activator_frames_pushed += to_push
	_activator_fill_calls += 1
	var bus_idx = AudioServer.get_bus_index(REVERB_BUS_NAME)
	var skips = playback.get_skips() if playback.has_method("get_skips") else -1
	activator_instrumentation = {
		"active": true,
		"frames_pushed_total": _activator_frames_pushed,
		"fill_calls": _activator_fill_calls,
		"bus_index": bus_idx,
		"bus_muted": AudioServer.is_bus_mute(bus_idx) if bus_idx >= 0 else true,
		"bus_send": str(AudioServer.get_bus_send(bus_idx)) if bus_idx >= 0 else "",
		"skips": skips,
	}

func _prepare_geometry_before_reinit():
	if get_tree():
		get_tree().call_group("resonance_geometry", "discard_meshes_before_scene_release")

func _reload_after_reinit():
	if get_tree():
		var static_scene = ResonanceSceneUtils.find_resonance_static_scene(get_tree().get_root())
		if static_scene and static_scene.static_scene_asset and static_scene.has_valid_asset():
			var srv = Engine.get_singleton("ResonanceServer")
			if srv and srv.has_method("load_static_scene_from_asset"):
				srv.load_static_scene_from_asset(static_scene.static_scene_asset)
		get_tree().call_group_flags(SceneTree.GROUP_CALL_DEFERRED, "resonance_probe_volume", "_reload_probe_batch_after_reinit")
		get_tree().call_group_flags(SceneTree.GROUP_CALL_DEFERRED, "resonance_geometry", "refresh_geometry")

func _connect_runtime_signals():
	if not Engine.is_editor_hint() or not _runtime:
		return
	if _runtime.has_signal("reflection_type_changed") and not _runtime.reflection_type_changed.is_connected(_on_reflection_type_changed):
		_runtime.reflection_type_changed.connect(_on_reflection_type_changed)
	if _runtime.has_signal("pathing_enabled_changed") and not _runtime.pathing_enabled_changed.is_connected(_on_runtime_affecting_probes_changed):
		_runtime.pathing_enabled_changed.connect(_on_runtime_affecting_probes_changed)

func _disconnect_runtime_signals():
	if not _runtime:
		return
	if _runtime.has_signal("reflection_type_changed") and _runtime.reflection_type_changed.is_connected(_on_reflection_type_changed):
		_runtime.reflection_type_changed.disconnect(_on_reflection_type_changed)
	if _runtime.has_signal("pathing_enabled_changed") and _runtime.pathing_enabled_changed.is_connected(_on_runtime_affecting_probes_changed):
		_runtime.pathing_enabled_changed.disconnect(_on_runtime_affecting_probes_changed)

func _on_reflection_type_changed(_arg = null):
	if not Engine.has_singleton("ResonanceServer"):
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if not srv or not srv.is_initialized():
		return
	var cfg = get_config_dict()
	if cfg.is_empty():
		return
	_prepare_geometry_before_reinit()
	srv.reinit_audio_engine(cfg)
	call_deferred("_reload_after_reinit")
	_notify_volumes_runtime_config_changed()

func _on_runtime_affecting_probes_changed(_arg = null):
	if Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv and srv.is_initialized():
			srv.set_pathing_enabled(_runtime.pathing_enabled)
			var removed = srv.revalidate_probe_batches_with_config()
			if removed > 0 and get_tree():
				get_tree().call_group_flags(SceneTree.GROUP_CALL_DEFERRED, "resonance_probe_volume", "reload_probe_batch")
	_notify_volumes_runtime_config_changed()

func _notify_volumes_runtime_config_changed():
	if not get_tree() or not _runtime:
		return
	var refl: int = _runtime.reflection_type
	var pathing: bool = _runtime.pathing_enabled
	get_tree().call_group_flags(SceneTree.GROUP_CALL_DEFERRED, "resonance_probe_volume", "notify_runtime_config_changed", refl, pathing)

func _warn_restart_if_needed():
	if Engine.is_editor_hint():
		return
	if Engine.has_singleton("ResonanceServer") and Engine.get_singleton("ResonanceServer").is_initialized():
		print_rich("[color=yellow][Nexus Resonance] Change requires game restart to take effect.[/color]")

func _update_debug_overlay_visibility():
	var overlay = get_node_or_null("DebugOverlay")
	if overlay:
		overlay.visible = debug_overlay_visible


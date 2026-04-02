@tool
@icon("res://addons/nexus_resonance/ui/icons/resonance_config_icon.svg")
class_name ResonanceRuntime
extends Node

## Nexus Resonance scene configuration node.
## Add via **Add Child Node** → **ResonanceRuntime** (global class; always includes this script).
## Assign a ResonanceRuntimeConfig resource (create new or link existing) for runtime settings.

const ResonanceRuntimeConfig = preload(
	"res://addons/nexus_resonance/scripts/resonance_runtime_config.gd"
)
const ResonanceBakeConfig = preload("res://addons/nexus_resonance/scripts/resonance_bake_config.gd")
const ResonanceSceneUtils = preload("res://addons/nexus_resonance/scripts/resonance_scene_utils.gd")
const ResonanceFMODBridgeScript = preload(
	"res://addons/nexus_resonance/scripts/resonance_fmod_bridge.gd"
)
const EFFECT_CLASS := "ResonanceAudioEffect"

## Custom Performance monitor IDs (Debugger → Monitors). Unregistered in [method _exit_tree].
const _NEXUS_PERF_MON_MAIN := "Nexus Resonance/Main/runtime_last_tick_us"
const _NEXUS_PERF_MON_W_DIRECT := "Nexus Resonance/Worker/us_run_direct"
const _NEXUS_PERF_MON_W_REFL := "Nexus Resonance/Worker/us_run_reflections"
const _NEXUS_PERF_MON_W_PATH := "Nexus Resonance/Worker/us_run_pathing"
const _NEXUS_PERF_MON_W_SYNC := "Nexus Resonance/Worker/us_sync_fetch"
const _NEXUS_PERF_MON_W_SUM := "Nexus Resonance/Worker/last_tick_sum_us"

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

@export_group("FMOD Bridge")
## Enable Steam Audio + FMOD bridge. Requires fmod-gdextension and phonon_fmod plugin in FMOD path.
@export var fmod_bridge_enabled: bool = false

@export_group("Runtime Debug")
## When off, overlay keys do nothing (hide dev tools from players). Turn on for development builds only.
@export var enable_debug: bool = false:
	get:
		return _enable_debug
	set(v):
		if _enable_debug == v:
			return
		var was_on := _enable_debug
		_enable_debug = v
		if was_on and not v:
			_disable_runtime_debug_ui()

## Key to toggle debug overlay (audio/server status, log).
@export var debug_overlay_toggle_key: Key = KEY_F1
## Key to toggle performance overlay (FPS, frame time). Off until first toggle; node is created lazily.
@export var performance_overlay_toggle_key: Key = KEY_F2
## Key to toggle player/source ray visualization (occlusion + reflection rays in engine). Requires ResonanceServer debug support.
@export var player_overlay_toggle_key: Key = KEY_F3

var _enable_debug: bool = false
var _debug_overlay_visible: bool = false
var _performance_overlay_visible: bool = false
## Effective source/reflection debug draw (was debug_sources bool).
var _player_overlay_visible: bool = false

var _fmod_bridge: RefCounted = null  # ResonanceFMODBridgeScript
var _activator: AudioStreamPlayer

## Reverb Bus activator instrumentation (for debugging convolution routing).
## Read from debug overlay via get_tree().get_first_node_in_group("resonance_runtime").
var activator_instrumentation: Dictionary = {}
var _activator_frames_pushed: int = 0
var _activator_fill_calls: int = 0

## Duration of the last [method _process] tick in microseconds (activator, listener, [method ResonanceServer.tick]). Shown in overlays; compare to worker timings from the ResonanceServer singleton.
var main_thread_last_tick_usec: int = 0


## Returns bake params from first Probe Volume with bake_config, or default. Used before init so pathing visibility params are set.
func _get_bake_params_for_runtime() -> Dictionary:
	if not is_inside_tree():
		return ResonanceBakeConfig.create_default().get_bake_params()
	var tree = get_tree()
	if not tree:
		return ResonanceBakeConfig.create_default().get_bake_params()
	var volumes = tree.get_nodes_in_group("resonance_probe_volume")
	for vol in volumes:
		var bc = vol.get("bake_config") if "bake_config" in vol else null
		if bc and bc.has_method("get_bake_params"):
			return bc.get_bake_params()
	return ResonanceBakeConfig.create_default().get_bake_params()


## Returns effective bus for Direct + Pathing. Public API for debug overlay and external use.
func get_bus_effective() -> StringName:
	return _get_bus_effective()


## Returns the FMOD bridge when fmod_bridge_enabled. Used by ResonanceFmodEventEmitter.
func get_fmod_bridge() -> RefCounted:
	return _fmod_bridge


## Returns config dict for init_audio_engine. Merges runtime config with debug_occlusion (player ray overlay on/off).
func get_config_dict() -> Dictionary:
	var cfg: Dictionary = _runtime.get_config() if _runtime else {}
	cfg["debug_occlusion"] = _player_overlay_visible
	return cfg


func _ready() -> void:
	if not _runtime:
		_runtime = ResonanceRuntimeConfig.create_default()
	add_to_group("resonance_runtime")
	set_process_priority(100)
	_connect_runtime_signals()
	if Engine.is_editor_hint():
		call_deferred("_notify_volumes_runtime_config_changed")
	if not get_node_or_null("DebugOverlay"):
		const DebugOverlayScript = preload("res://addons/nexus_resonance/scripts/debug_overlay.gd")
		var overlay = DebugOverlayScript.new()
		overlay.name = "DebugOverlay"
		add_child(overlay)
	_initialize_server()
	if not Engine.is_editor_hint():
		_setup_activator()
		call_deferred("_apply_bus_to_players")
	_update_debug_overlay_visibility()
	if not Engine.is_editor_hint():
		_register_nexus_performance_monitors()


func _exit_tree() -> void:
	_unregister_nexus_performance_monitors()
	var perf_overlay = get_node_or_null("PerformanceOverlay")
	if perf_overlay:
		perf_overlay.visible = false
		perf_overlay.process_mode = Node.PROCESS_MODE_DISABLED
	if _fmod_bridge:
		_fmod_bridge.shutdown_bridge()
		_fmod_bridge = null


func _input(event: InputEvent) -> void:
	if Engine.is_editor_hint():
		return
	if not _enable_debug:
		return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == debug_overlay_toggle_key:
			_toggle_debug_overlay()
			get_viewport().set_input_as_handled()
		elif event.keycode == performance_overlay_toggle_key:
			_toggle_performance_overlay()
			get_viewport().set_input_as_handled()
		elif event.keycode == player_overlay_toggle_key:
			_toggle_player_overlay()
			get_viewport().set_input_as_handled()


func _init_fmod_bridge() -> void:
	_fmod_bridge = ResonanceFMODBridgeScript.new()
	if _fmod_bridge.init_bridge():
		pass  # Success; bridge is ready
	else:
		push_warning(
			"Nexus Resonance: FMOD bridge init failed. Ensure phonon_fmod plugin is in FMOD path."
		)


func _toggle_debug_overlay() -> void:
	_debug_overlay_visible = not _debug_overlay_visible
	_update_debug_overlay_visibility()


func _create_performance_overlay() -> void:
	if Engine.is_editor_hint() or get_node_or_null("PerformanceOverlay"):
		return
	const PerformanceOverlayScript = preload(
		"res://addons/nexus_resonance/scripts/performance_overlay.gd"
	)
	var perf_overlay = PerformanceOverlayScript.new()
	perf_overlay.name = "PerformanceOverlay"
	perf_overlay.visible = false
	add_child(perf_overlay)


func _toggle_performance_overlay() -> void:
	_performance_overlay_visible = not _performance_overlay_visible
	var overlay = get_node_or_null("PerformanceOverlay")
	if overlay == null and _performance_overlay_visible:
		_create_performance_overlay()
		overlay = get_node_or_null("PerformanceOverlay")
	if overlay:
		overlay.visible = _performance_overlay_visible
		if _performance_overlay_visible:
			overlay.process_mode = Node.PROCESS_MODE_INHERIT


func _toggle_player_overlay() -> void:
	_player_overlay_visible = not _player_overlay_visible
	_apply_debug_flags()
	_refresh_resonance_geometry_for_debug_viz()


func _disable_runtime_debug_ui() -> void:
	_debug_overlay_visible = false
	_performance_overlay_visible = false
	_player_overlay_visible = false
	_update_debug_overlay_visibility()
	var perf := get_node_or_null("PerformanceOverlay")
	if perf:
		perf.visible = false
		perf.process_mode = Node.PROCESS_MODE_DISABLED
	_apply_debug_flags()
	_refresh_resonance_geometry_for_debug_viz()


func _refresh_resonance_geometry_for_debug_viz() -> void:
	if not is_inside_tree():
		return
	if not Engine.has_singleton("ResonanceServer"):
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if not srv.is_initialized():
		return
	# sync_reflection_debug_viz updates RayTraceDebugContext only. Full refresh_geometry() rebuilds
	# IPL InstancedMesh/sub-scenes and breaks Embree dynamic occlusion when toggling F3.
	get_tree().call_group_flags(
		SceneTree.GROUP_CALL_DEFERRED, "resonance_geometry", "sync_reflection_debug_viz"
	)


## CollisionObject3D RIDs along the active camera parent chain plus [code]resonance_listener[/code] group nodes.
## Used with Custom scene type so occlusion/reflection rays do not hit the listener body.
func _collect_listener_physics_exclude_rids(vp: Viewport) -> Array[RID]:
	var out: Array[RID] = []
	var cam = vp.get_camera_3d()
	if cam:
		var n: Node = cam
		while n:
			if n is CollisionObject3D:
				out.append(n.get_rid())
			n = n.get_parent()
	var tree = get_tree()
	if tree:
		var listeners = tree.get_nodes_in_group("resonance_listener")
		for node in listeners:
			if node is CollisionObject3D:
				out.append(node.get_rid())
	return out


func _process(delta: float) -> void:
	if Engine.is_editor_hint():
		return
	var t0_usec := Time.get_ticks_usec()
	_fill_activator_buffer()
	if Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv.is_initialized():
			# Runtime frame_size detection: reverb bus may have requested reinit with actual Godot frame_count
			var pending: int = srv.consume_pending_reinit_frame_size()
			if pending > 0:
				var cfg := get_config_dict()
				cfg["audio_frame_size"] = pending
				_prepare_geometry_before_reinit()
				srv.reinit_audio_engine(cfg)
				call_deferred("_reload_after_reinit")
			var vp = get_viewport()
			if vp:
				if srv.has_method("set_physics_world"):
					var w3 = vp.get_world_3d()
					if w3:
						srv.set_physics_world(w3)
				if srv.uses_custom_ray_tracer() and srv.has_method("set_listener_physics_ray_exclude_rids"):
					srv.set_listener_physics_ray_exclude_rids(_collect_listener_physics_exclude_rids(vp))
				var cam = vp.get_camera_3d()
				if cam and is_inside_tree():
					var listeners = get_tree().get_nodes_in_group("resonance_listener")
					if listeners.is_empty():
						srv.update_listener(
							cam.global_position,
							-cam.global_transform.basis.z,
							cam.global_transform.basis.y
						)
			srv.tick(delta)
			if srv.has_method("flush_pending_source_updates"):
				srv.flush_pending_source_updates()
	var dt_usec := Time.get_ticks_usec() - t0_usec
	main_thread_last_tick_usec = dt_usec if dt_usec > 0 else 0


func _initialize_server() -> void:
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
	# FMOD Bridge: Connect Steam Audio to FMOD when enabled
	if fmod_bridge_enabled:
		_init_fmod_bridge()
	# Unity-style: load static scene(s) from ResonanceStaticScene nodes. Additive: one per scene.
	var static_scenes: Array[Node] = []
	ResonanceSceneUtils.collect_resonance_static_scenes(get_tree().get_root(), static_scenes)
	if not static_scenes.is_empty():
		if srv.has_method("clear_static_scenes") and srv.has_method("add_static_scene_from_asset"):
			srv.clear_static_scenes()
			for ss in static_scenes:
				if ss.static_scene_asset and ss.has_valid_asset():
					srv.add_static_scene_from_asset(
						ss.static_scene_asset, ss.get_global_transform()
					)
		# Legacy single-scene API; prefer add_static_scene_from_asset when multiple scenes.
		elif srv.has_method("load_static_scene_from_asset") and static_scenes.size() == 1:
			if static_scenes[0].static_scene_asset and static_scenes[0].has_valid_asset():
				srv.load_static_scene_from_asset(
					static_scenes[0].static_scene_asset, static_scenes[0].get_global_transform()
				)
	# add_static_scene_from_asset / load_static_scene_from_asset commit the IPL scene and set scene_dirty;
	# the simulation worker runs iplSceneCommit + RunDirect on the next tick. Deferred refresh re-runs
	# ResonanceGeometry::_create_meshes for nodes that missed server init during _ready (same as reinit path).
	if is_inside_tree():
		var tree = get_tree()
		if tree:
			tree.call_group_flags(
				SceneTree.GROUP_CALL_DEFERRED, "resonance_geometry", "refresh_geometry"
			)
	_apply_debug_flags()
	_apply_perspective_correction()
	# Mute spatialized player output until several worker RunDirect ticks after scene/geometry settle (see kSpatialAudioWarmupWorkerPasses).
	call_deferred("_deferred_reset_spatial_audio_warmup_passes")


func _deferred_reset_spatial_audio_warmup_passes() -> void:
	if not Engine.has_singleton("ResonanceServer"):
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if srv.is_initialized() and srv.has_method("reset_spatial_audio_warmup_passes"):
		srv.reset_spatial_audio_warmup_passes()


func _apply_debug_flags() -> void:
	if not Engine.has_singleton("ResonanceServer"):
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if srv.is_initialized():
		srv.set_debug_occlusion(_player_overlay_visible)
		if srv.has_method("set_debug_reflections"):
			srv.set_debug_reflections(_player_overlay_visible)


func _apply_perspective_correction() -> void:
	if not Engine.has_singleton("ResonanceServer"):
		return
	var srv = Engine.get_singleton("ResonanceServer")
	if srv.is_initialized():
		srv.set_perspective_correction_enabled(runtime.perspective_correction_enabled)
		srv.set_perspective_correction_factor(runtime.perspective_correction_factor)
		srv.set_reverb_transmission_amount(runtime.reverb_transmission_amount)


func _get_bus_effective() -> StringName:
	if _runtime:
		return _runtime.get_bus_effective()
	return &"Master"


func _get_reverb_bus_name() -> StringName:
	return _runtime.get_reverb_bus_name_effective() if _runtime else &"ResonanceReverb"


func _get_reverb_bus_send() -> StringName:
	## Reverb output goes to same bus as Direct+Pathing. No separate send bus.
	return _get_bus_effective()


func _ensure_reverb_bus_exists() -> bool:
	var bus_name := _get_reverb_bus_name()
	var send_name := _get_reverb_bus_send()
	var idx = AudioServer.get_bus_index(bus_name)
	if idx == -1:
		AudioServer.add_bus()
		idx = AudioServer.bus_count - 1
		AudioServer.set_bus_name(idx, bus_name)
		if idx > 1:
			AudioServer.move_bus(idx, 1)
			idx = 1
		if ClassDB.class_exists(EFFECT_CLASS):
			var effect = ClassDB.instantiate(EFFECT_CLASS)
			effect.resource_name = "Resonance Reverb"
			AudioServer.add_bus_effect(idx, effect)
	if idx >= 0:
		AudioServer.set_bus_send(idx, send_name)
	return idx >= 0


func _apply_bus_to_players() -> void:
	if not is_inside_tree():
		return
	var tree = get_tree()
	if not tree:
		return
	var global_bus := _get_bus_effective()
	var reverb_bus := _get_reverb_bus_send()
	var reflection_type := -1
	if Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv and srv.is_initialized() and srv.has_method("get_reflection_type"):
			reflection_type = srv.get_reflection_type()
	var players = tree.get_nodes_in_group("resonance_player")
	for p in players:
		var cfg = p.get("player_config") if "player_config" in p else null
		var effective_bus: StringName
		if cfg and cfg.has_method("get_bus_name_effective"):
			effective_bus = cfg.get_bus_name_effective(global_bus)
		else:
			effective_bus = global_bus
		if p.has_method("set_bus"):
			p.set_bus(effective_bus)
		# Parametric (1) / Hybrid (2): enable split when bus != reverb_bus
		var reverb_split := (
			(reflection_type == 1 or reflection_type == 2) and str(effective_bus) != str(reverb_bus)
		)
		if p.has_method("set_reverb_split_output"):
			p.set_reverb_split_output(reverb_split, reverb_bus)


func _setup_activator() -> void:
	if not _ensure_reverb_bus_exists():
		return
	var bus_name := _get_reverb_bus_name()
	_activator = AudioStreamPlayer.new()
	_activator.name = "ResonanceInternalActivator"
	_activator.bus = bus_name
	var gen = AudioStreamGenerator.new()
	gen.buffer_length = 0.1
	_activator.stream = gen
	add_child(_activator)
	_activator.play()


func _fill_activator_buffer() -> void:
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
		activator_instrumentation["avail_zero_count"] = (
			activator_instrumentation.get("avail_zero_count", 0) + 1
		)
		return
	const AMP := 1e-5
	var to_push = min(avail, 512)
	for i in to_push:
		playback.push_frame(Vector2(AMP, AMP))
	_activator_frames_pushed += to_push
	_activator_fill_calls += 1
	var bus_idx = AudioServer.get_bus_index(_get_reverb_bus_name())
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


func _prepare_geometry_before_reinit() -> void:
	if is_inside_tree():
		get_tree().call_group("resonance_geometry", "discard_meshes_before_scene_release")


func _reload_after_reinit() -> void:
	if not is_inside_tree():
		return
	var tree = get_tree()
	var static_scene = ResonanceSceneUtils.find_resonance_static_scene(tree.get_root())
	if static_scene and static_scene.static_scene_asset and static_scene.has_valid_asset():
		var srv = Engine.get_singleton("ResonanceServer")
		if srv and srv.has_method("load_static_scene_from_asset"):
			srv.load_static_scene_from_asset(
				static_scene.static_scene_asset, static_scene.get_global_transform()
			)
	tree.call_group_flags(
		SceneTree.GROUP_CALL_DEFERRED,
		"resonance_probe_volume",
		"_reload_probe_batch_after_reinit"
	)
	tree.call_group_flags(
		SceneTree.GROUP_CALL_DEFERRED, "resonance_geometry", "refresh_geometry"
	)
	call_deferred("_deferred_reset_spatial_audio_warmup_passes")


func _connect_runtime_signals() -> void:
	if not Engine.is_editor_hint() or not _runtime:
		return
	if (
		_runtime.has_signal("reflection_type_changed")
		and not _runtime.reflection_type_changed.is_connected(_on_reflection_type_changed)
	):
		_runtime.reflection_type_changed.connect(_on_reflection_type_changed)
	if (
		_runtime.has_signal("pathing_enabled_changed")
		and not _runtime.pathing_enabled_changed.is_connected(_on_runtime_affecting_probes_changed)
	):
		_runtime.pathing_enabled_changed.connect(_on_runtime_affecting_probes_changed)
	if (
		_runtime.has_signal("audio_frame_size_changed")
		and not _runtime.audio_frame_size_changed.is_connected(_on_audio_frame_size_changed)
	):
		_runtime.audio_frame_size_changed.connect(_on_audio_frame_size_changed)


func _disconnect_runtime_signals() -> void:
	if not _runtime:
		return
	if (
		_runtime.has_signal("reflection_type_changed")
		and _runtime.reflection_type_changed.is_connected(_on_reflection_type_changed)
	):
		_runtime.reflection_type_changed.disconnect(_on_reflection_type_changed)
	if (
		_runtime.has_signal("pathing_enabled_changed")
		and _runtime.pathing_enabled_changed.is_connected(_on_runtime_affecting_probes_changed)
	):
		_runtime.pathing_enabled_changed.disconnect(_on_runtime_affecting_probes_changed)
	if (
		_runtime.has_signal("audio_frame_size_changed")
		and _runtime.audio_frame_size_changed.is_connected(_on_audio_frame_size_changed)
	):
		_runtime.audio_frame_size_changed.disconnect(_on_audio_frame_size_changed)


func _on_reflection_type_changed(_arg = null) -> void:
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


func _on_audio_frame_size_changed(_arg = null) -> void:
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


func _on_runtime_affecting_probes_changed(_arg = null) -> void:
	if Engine.has_singleton("ResonanceServer"):
		var srv = Engine.get_singleton("ResonanceServer")
		if srv and srv.is_initialized():
			srv.set_pathing_enabled(_runtime.pathing_enabled)
			var removed = srv.revalidate_probe_batches_with_config()
			if removed > 0 and is_inside_tree():
				get_tree().call_group_flags(
					SceneTree.GROUP_CALL_DEFERRED, "resonance_probe_volume", "reload_probe_batch"
				)
	_notify_volumes_runtime_config_changed()


func _notify_volumes_runtime_config_changed() -> void:
	if not is_inside_tree() or not _runtime:
		return
	var tree = get_tree()
	var refl: int = _runtime.reflection_type
	var pathing: bool = _runtime.pathing_enabled
	tree.call_group_flags(
		SceneTree.GROUP_CALL_DEFERRED,
		"resonance_probe_volume",
		"notify_runtime_config_changed",
		refl,
		pathing
	)


func _warn_restart_if_needed() -> void:
	if Engine.is_editor_hint():
		return
	if (
		Engine.has_singleton("ResonanceServer")
		and Engine.get_singleton("ResonanceServer").is_initialized()
	):
		print_rich(
			"[color=yellow][Nexus Resonance] Change requires game restart to take effect.[/color]"
		)


func _register_nexus_performance_monitors() -> void:
	var existing: Array = Performance.get_custom_monitor_names()
	var ids: Array[String] = [
		_NEXUS_PERF_MON_MAIN,
		_NEXUS_PERF_MON_W_DIRECT,
		_NEXUS_PERF_MON_W_REFL,
		_NEXUS_PERF_MON_W_PATH,
		_NEXUS_PERF_MON_W_SYNC,
		_NEXUS_PERF_MON_W_SUM,
	]
	for id in ids:
		if id in existing:
			Performance.remove_custom_monitor(id)
	Performance.add_custom_monitor(_NEXUS_PERF_MON_MAIN, Callable(self, "_nexus_perf_read_main_usec"))
	Performance.add_custom_monitor(
		_NEXUS_PERF_MON_W_DIRECT, Callable(self, "_nexus_perf_worker_field").bind("us_run_direct")
	)
	Performance.add_custom_monitor(
		_NEXUS_PERF_MON_W_REFL, Callable(self, "_nexus_perf_worker_field").bind("us_run_reflections")
	)
	Performance.add_custom_monitor(
		_NEXUS_PERF_MON_W_PATH, Callable(self, "_nexus_perf_worker_field").bind("us_run_pathing")
	)
	Performance.add_custom_monitor(
		_NEXUS_PERF_MON_W_SYNC, Callable(self, "_nexus_perf_worker_field").bind("us_sync_fetch")
	)
	Performance.add_custom_monitor(_NEXUS_PERF_MON_W_SUM, Callable(self, "_nexus_perf_read_worker_sum"))


func _unregister_nexus_performance_monitors() -> void:
	if Engine.is_editor_hint():
		return
	var ids: Array[String] = [
		_NEXUS_PERF_MON_MAIN,
		_NEXUS_PERF_MON_W_DIRECT,
		_NEXUS_PERF_MON_W_REFL,
		_NEXUS_PERF_MON_W_PATH,
		_NEXUS_PERF_MON_W_SYNC,
		_NEXUS_PERF_MON_W_SUM,
	]
	for id in ids:
		Performance.remove_custom_monitor(id)


func _nexus_perf_read_main_usec() -> int:
	return main_thread_last_tick_usec


func _nexus_perf_worker_field(field: String) -> int:
	if not Engine.has_singleton("ResonanceServer"):
		return 0
	var srv = Engine.get_singleton("ResonanceServer")
	if not srv.is_initialized() or not srv.has_method("get_simulation_worker_timing"):
		return 0
	return int(srv.get_simulation_worker_timing().get(field, 0))


func _nexus_perf_read_worker_sum() -> int:
	if not Engine.has_singleton("ResonanceServer"):
		return 0
	var srv = Engine.get_singleton("ResonanceServer")
	if not srv.is_initialized() or not srv.has_method("get_simulation_worker_timing"):
		return 0
	var w: Dictionary = srv.get_simulation_worker_timing()
	return (
		int(w.get("us_run_direct", 0))
		+ int(w.get("us_run_reflections", 0))
		+ int(w.get("us_run_pathing", 0))
		+ int(w.get("us_sync_fetch", 0))
	)


func _update_debug_overlay_visibility() -> void:
	var overlay = get_node_or_null("DebugOverlay")
	if overlay:
		overlay.visible = _debug_overlay_visible

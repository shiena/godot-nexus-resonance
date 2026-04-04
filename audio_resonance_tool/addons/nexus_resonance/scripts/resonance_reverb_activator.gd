extends RefCounted
class_name ResonanceReverbActivator

## Keeps the reverb bus active via a low-level [AudioStreamGenerator] feed (see [ResonanceRuntime]).

var _player: AudioStreamPlayer
var _frames_pushed: int = 0
var _fill_calls: int = 0
## Instrumentation for debug overlay ([member ResonanceRuntime.activator_instrumentation] delegates here).
var instrumentation: Dictionary = {}


func setup(parent: Node, bus: ResonanceRuntimeBus) -> bool:
	if not bus.ensure_reverb_bus_exists():
		return false
	var bus_name := bus.get_reverb_bus_name()
	_player = AudioStreamPlayer.new()
	_player.name = "ResonanceInternalActivator"
	_player.bus = bus_name
	var gen = AudioStreamGenerator.new()
	gen.buffer_length = 0.1
	_player.stream = gen
	parent.add_child(_player)
	_player.play()
	return true


func fill_buffer(bus: ResonanceRuntimeBus) -> void:
	if not _player or not _player.playing:
		instrumentation = {"active": false, "reason": "no_activator_or_not_playing"}
		return
	var playback = _player.get_stream_playback()
	if not playback:
		instrumentation = {"active": false, "reason": "no_playback"}
		return
	if not playback is AudioStreamGeneratorPlayback:
		instrumentation = {"active": false, "reason": "not_generator"}
		return
	var avail = playback.get_frames_available()
	if avail <= 0:
		instrumentation["active"] = true
		instrumentation["avail_zero_count"] = instrumentation.get("avail_zero_count", 0) + 1
		return
	const AMP := 1e-5
	var to_push = min(avail, 512)
	for i in to_push:
		playback.push_frame(Vector2(AMP, AMP))
	_frames_pushed += to_push
	_fill_calls += 1
	var bus_idx = AudioServer.get_bus_index(bus.get_reverb_bus_name())
	var skips = playback.get_skips() if playback.has_method("get_skips") else -1
	instrumentation = {
		"active": true,
		"frames_pushed_total": _frames_pushed,
		"fill_calls": _fill_calls,
		"bus_index": bus_idx,
		"bus_muted": AudioServer.is_bus_mute(bus_idx) if bus_idx >= 0 else true,
		"bus_send": str(AudioServer.get_bus_send(bus_idx)) if bus_idx >= 0 else "",
		"skips": skips,
	}

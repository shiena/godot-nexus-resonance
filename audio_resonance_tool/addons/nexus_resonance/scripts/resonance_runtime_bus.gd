extends RefCounted
class_name ResonanceRuntimeBus

## Reverb bus creation and routing for [ResonanceRuntime] (AudioServer + [ResonancePlayer] group).

const EFFECT_CLASS := "ResonanceAudioEffect"

var _get_bus_effective: Callable
var _get_reverb_bus_name: Callable
var _get_reverb_bus_send: Callable


func _init(
	get_bus_effective_cb: Callable, get_reverb_bus_name_cb: Callable, get_reverb_bus_send_cb: Callable
) -> void:
	_get_bus_effective = get_bus_effective_cb
	_get_reverb_bus_name = get_reverb_bus_name_cb
	_get_reverb_bus_send = get_reverb_bus_send_cb


func get_bus_effective() -> StringName:
	return _get_bus_effective.call()


func get_reverb_bus_name() -> StringName:
	return _get_reverb_bus_name.call()


func get_reverb_bus_send() -> StringName:
	return _get_reverb_bus_send.call()


func ensure_reverb_bus_exists() -> bool:
	var bus_name := get_reverb_bus_name()
	var send_name := get_reverb_bus_send()
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


func apply_bus_to_players(tree: SceneTree) -> void:
	if tree == null:
		return
	var global_bus := get_bus_effective()
	var reverb_bus := get_reverb_bus_send()
	var reflection_type := -1
	var srv: Variant = ResonanceServerAccess.get_server_if_initialized()
	if srv != null and srv.has_method("get_reflection_type"):
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
		var reverb_split := (reflection_type == 1 or reflection_type == 2) and effective_bus != reverb_bus
		if p.has_method("set_reverb_split_output"):
			p.set_reverb_split_output(reverb_split, reverb_bus)

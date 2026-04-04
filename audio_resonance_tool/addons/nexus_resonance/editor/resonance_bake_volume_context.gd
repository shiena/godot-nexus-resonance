extends RefCounted
class_name VolumeBakeContext

## Per-volume bake state for one pipeline step (reflections, pathing, static source/listener).

const _Self = preload("res://addons/nexus_resonance/editor/resonance_bake_volume_context.gd")
const ResonanceBakeConfig = preload("res://addons/nexus_resonance/scripts/resonance_bake_config.gd")
const _BakeDiscovery = preload("res://addons/nexus_resonance/editor/resonance_bake_discovery.gd")
const _BakeHashes = preload("res://addons/nexus_resonance/editor/resonance_bake_hashes.gd")

var root: Node
var vol: Node
var probe_data: Resource
var need_reflections: bool
var need_pathing: bool
var need_static_source: bool
var need_static_listener: bool
var add_flags: Dictionary
var player_pos: Vector3
var player_radius: float
var listener_pos: Vector3
var listener_radius: float
var bc: Resource
var vol_info: String
var static_asset = null  # ResonanceGeometryAsset used for bake; for static_scene_params_hash


static func build(
	vol: Node,
	root: Node,
	vol_index: int,
	total: int,
	static_asset,
	get_bake_config_for_volume: Callable,
	default_influence_radius: float
):
	var ctx = _Self.new()
	ctx.root = root
	ctx.vol = vol
	ctx.static_asset = static_asset
	ctx.vol_info = " (volume %d of %d)" % [vol_index, total] if total > 1 else ""
	ctx.bc = get_bake_config_for_volume.call(vol) if get_bake_config_for_volume.is_valid() else null
	if ctx.bc == null:
		ctx.bc = ResonanceBakeConfig.create_default()
	ctx.add_flags = {
		"static_source": ctx.bc.static_source_enabled, "static_listener": ctx.bc.static_listener_enabled
	}
	ctx.player_pos = Vector3.ZERO
	ctx.player_radius = (
		vol.get("bake_influence_radius")
		if "bake_influence_radius" in vol
		else default_influence_radius
	)
	ctx.listener_pos = Vector3.ZERO
	ctx.listener_radius = (
		vol.get("bake_influence_radius")
		if "bake_influence_radius" in vol
		else default_influence_radius
	)
	if ctx.add_flags.static_source:
		var src = _BakeDiscovery.resolve_bake_node_for_volume(
			vol, root, "bake_sources", "ResonancePlayer"
		)
		if src and src is Node3D:
			ctx.player_pos = src.global_position
	if ctx.add_flags.static_listener:
		var lst = _BakeDiscovery.resolve_bake_node_for_volume(
			vol, root, "bake_listeners", "ResonanceListener"
		)
		if lst and lst is Node3D:
			ctx.listener_pos = lst.global_position
	var want_path = ctx.bc.pathing_enabled
	var path_hash = _BakeHashes.compute_pathing_hash(ctx.bc) if want_path else 0
	var probe_data = vol.get_probe_data() if vol.has_method("get_probe_data") else null
	if not probe_data:
		probe_data = ClassDB.instantiate("ResonanceProbeData")
		vol.set_probe_data(probe_data)
	ctx.probe_data = probe_data
	var ph = (
		probe_data.get_pathing_params_hash()
		if probe_data.has_method("get_pathing_params_hash")
		else 0
	)
	var has_data = probe_data.get_data().size() > 0
	var desired_refl = ctx.bc.reflection_type
	var refl_matches = (
		probe_data.get_baked_reflection_type() == desired_refl
		if probe_data.has_method("get_baked_reflection_type")
		else false
	)
	var hash_matches = (
		probe_data.get_bake_params_hash() == vol.get_bake_params_hash()
		if vol.has_method("get_bake_params_hash")
		else false
	)
	ctx.need_reflections = not has_data or not hash_matches or not refl_matches
	if static_asset and ResonanceServerAccess.has_server():
		var srv = ResonanceServerAccess.get_server()
		if (
			srv.has_method("get_geometry_asset_hash")
			and probe_data.has_method("get_static_scene_params_hash")
		):
			var current_hash: int = srv.get_geometry_asset_hash(static_asset)
			var stored: int = probe_data.get_static_scene_params_hash()
			if stored == 0 or stored != current_hash:
				ctx.need_reflections = true
	if not want_path and ph > 0:
		ctx.need_reflections = true
	ctx.need_pathing = want_path and (ph == 0 or ph != path_hash)
	if want_path and ctx.need_pathing and (not has_data or not refl_matches):
		ctx.need_reflections = true
	ctx.need_static_source = false
	ctx.need_static_listener = false
	if ctx.add_flags.static_source:
		var sh = _BakeHashes.compute_position_radius_hash(ctx.player_pos, ctx.player_radius)
		var ssh = (
			probe_data.get_static_source_params_hash()
			if probe_data.has_method("get_static_source_params_hash")
			else 0
		)
		ctx.need_static_source = ssh == 0 or ssh != sh
	if ctx.add_flags.static_listener:
		var lh = _BakeHashes.compute_position_radius_hash(ctx.listener_pos, ctx.listener_radius)
		var lsh = (
			probe_data.get_static_listener_params_hash()
			if probe_data.has_method("get_static_listener_params_hash")
			else 0
		)
		ctx.need_static_listener = lsh == 0 or lsh != lh
	return ctx

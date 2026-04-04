extends Object
class_name ResonanceBakeHashes

## Hash helpers for bake params and static source/listener positions (shared by context + status UI).


static func hash_dict(d: Dictionary) -> int:
	return hash(var_to_str(d))


static func compute_pathing_hash(bc: Resource) -> int:
	var params = bc.get_bake_params()
	return hash_dict(
		{
			"vis_range": params.get("bake_pathing_vis_range", 500),
			"path_range": params.get("bake_pathing_path_range", 100),
			"num_samples": params.get("bake_pathing_num_samples", 16),
			"radius": params.get("bake_pathing_radius", 0.5),
			"threshold": params.get("bake_pathing_threshold", 0.1)
		}
	)


static func compute_position_radius_hash(pos: Vector3, radius: float) -> int:
	return hash_dict({"pos": pos, "radius": radius})

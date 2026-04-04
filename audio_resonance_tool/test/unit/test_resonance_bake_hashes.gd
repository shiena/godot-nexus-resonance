extends GutTest

## Unit tests for [ResonanceBakeHashes]. Pathing hash must stay aligned with
## [code]test_resonance_bake_settings.gd[/code] ([code]_pathing_hash_from_params[/code]) and C++ baker.

const BakeHashes = preload("res://addons/nexus_resonance/editor/resonance_bake_hashes.gd")


## Same structure as [member test_resonance_bake_settings._pathing_hash_from_params] — keep in sync.
static func _pathing_hash_reference(params: Dictionary) -> int:
	return hash(
		var_to_str(
			{
				"vis_range": params.get("bake_pathing_vis_range", 500),
				"path_range": params.get("bake_pathing_path_range", 100),
				"num_samples": params.get("bake_pathing_num_samples", 16),
				"radius": params.get("bake_pathing_radius", 0.5),
				"threshold": params.get("bake_pathing_threshold", 0.1)
			}
		)
	)


func test_hash_dict_deterministic():
	var d := {"a": 1, "b": 2.0}
	var h1 := BakeHashes.hash_dict(d)
	var h2 := BakeHashes.hash_dict(d)
	assert_eq(h1, h2)
	assert_ne(h1, 0)


func test_hash_dict_changes_when_dict_changes():
	var h1 := BakeHashes.hash_dict({"k": 1})
	var h2 := BakeHashes.hash_dict({"k": 2})
	assert_ne(h1, h2)


func test_compute_pathing_hash_matches_reference_helper():
	var bc := ResonanceBakeConfig.create_default()
	bc.pathing_enabled = true
	var params := bc.get_bake_params()
	var ref := _pathing_hash_reference(params)
	assert_eq(BakeHashes.compute_pathing_hash(bc), ref)
	bc.bake_pathing_vis_range = 750.0
	bc.bake_pathing_threshold = 0.05
	params = bc.get_bake_params()
	ref = _pathing_hash_reference(params)
	assert_eq(BakeHashes.compute_pathing_hash(bc), ref)


func test_compute_position_radius_hash_deterministic():
	var h1 := BakeHashes.compute_position_radius_hash(Vector3(1, 2, 3), 0.25)
	var h2 := BakeHashes.compute_position_radius_hash(Vector3(1, 2, 3), 0.25)
	assert_eq(h1, h2)
	assert_ne(h1, BakeHashes.compute_position_radius_hash(Vector3(0, 0, 0), 0.25))

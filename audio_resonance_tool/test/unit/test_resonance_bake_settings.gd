extends GutTest

## Unit tests for ResonanceBakeConfig (per-volume bake resource).

func test_get_bake_params_has_expected_keys():
	var bc = ResonanceBakeConfig.create_default()
	var params = bc.get_bake_params()
	var expected = ["bake_num_rays", "bake_num_bounces", "bake_num_threads", "bake_reflection_type",
		"bake_pathing_vis_range", "bake_pathing_path_range", "bake_pathing_num_samples",
		"bake_pathing_radius", "bake_pathing_threshold"]
	for key in expected:
		assert_has(params, key, "get_bake_params missing: " + key)
	assert_eq(params.size(), expected.size(), "get_bake_params should have exactly expected keys")

func test_bake_defaults_are_sensible():
	var bc = ResonanceBakeConfig.create_default()
	assert_gte(bc.bake_num_rays, 256, "bake_num_rays should be >= 256")
	assert_lte(bc.bake_num_rays, 16384, "bake_num_rays should be <= 16384")
	assert_gte(bc.bake_num_bounces, 1, "bake_num_bounces should be >= 1")
	assert_lte(bc.bake_num_bounces, 32, "bake_num_bounces should be <= 32")
	assert_gte(bc.reflection_type, 0, "reflection_type should be 0-2")
	assert_lte(bc.reflection_type, 2, "reflection_type should be 0-2")

func test_get_bake_params_applies_properties():
	var bc = ResonanceBakeConfig.create_default()
	bc.bake_num_rays = 8192
	bc.pathing_enabled = true
	bc.bake_pathing_radius = 0.8
	var params = bc.get_bake_params()
	assert_eq(params.get("bake_num_rays", -1), 8192, "bake_num_rays should be applied")
	assert_eq(params.get("bake_pathing_radius", -1.0), 0.8, "bake_pathing_radius should be applied")

func test_create_default_returns_valid_config():
	var bc = ResonanceBakeConfig.create_default()
	assert_not_null(bc, "create_default should return non-null")
	assert_true(bc is ResonanceBakeConfig, "create_default should return ResonanceBakeConfig")

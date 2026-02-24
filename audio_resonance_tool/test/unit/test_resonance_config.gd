extends GutTest

## Unit tests for ResonanceRuntimeConfig (config resource).

func test_create_default_has_required_keys():
	var rt = ResonanceRuntimeConfig.create_default()
	var config = rt.get_config()
	var required = ["sample_rate", "ambisonic_order", "max_reverb_duration", "reverb_influence_radius"]
	for key in required:
		assert_has(config, key, "config missing: " + key)

func test_get_config_applies_properties():
	var rt = ResonanceRuntimeConfig.create_default()
	rt.ambisonic_order = 2
	rt.pathing_enabled = true
	var config = rt.get_config()
	assert_eq(config.get("ambisonic_order", -1), 2, "ambisonic_order not applied")
	assert_true(config.get("pathing_enabled", false), "pathing_enabled not applied")

func test_get_config_sample_rate_valid():
	var rt = ResonanceRuntimeConfig.create_default()
	var config = rt.get_config()
	assert_eq(typeof(config.get("sample_rate", 0)), TYPE_INT, "sample_rate should be int")
	assert_gt(config.get("sample_rate", 0), 0, "sample_rate should be > 0")

# --- get_effective_realtime_rays (Android fallback) ---

func test_effective_realtime_rays_android_forces_zero():
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(2048, "Android"), 0, "Android should force 0")
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(512, "Android"), 0, "Android should force 0 even for low value")

func test_effective_realtime_rays_android_zero_stays_zero():
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(0, "Android"), 0, "Android 0 stays 0")

func test_effective_realtime_rays_windows_passthrough():
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(2048, "Windows"), 2048, "Windows should pass through")
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(0, "Windows"), 0, "Windows 0 stays 0")

func test_effective_realtime_rays_linux_passthrough():
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(1024, "Linux"), 1024, "Linux should pass through")

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
	rt.scene_type = 2
	var config = rt.get_config()
	assert_eq(config.get("ambisonic_order", -1), 2, "ambisonic_order not applied")
	assert_true(config.get("pathing_enabled", false), "pathing_enabled not applied")
	assert_eq(config.get("scene_type", -1), 2, "scene_type not applied")

func test_get_config_sample_rate_valid():
	var rt = ResonanceRuntimeConfig.create_default()
	var config = rt.get_config()
	assert_eq(typeof(config.get("sample_rate", 0)), TYPE_INT, "sample_rate should be int")
	assert_gt(config.get("sample_rate", 0), 0, "sample_rate should be > 0")

func test_sample_rate_override_when_zero_uses_mix_rate():
	var rt = ResonanceRuntimeConfig.create_default()
	rt.sample_rate_override = 0
	var config = rt.get_config()
	var mix_rate := int(AudioServer.get_mix_rate())
	assert_eq(config.get("sample_rate", -1), mix_rate, "sample_rate_override=0 should use Godot mix rate")

func test_sample_rate_override_when_nonzero_uses_override():
	var rt = ResonanceRuntimeConfig.create_default()
	rt.sample_rate_override = 44100
	var config = rt.get_config()
	assert_eq(config.get("sample_rate", -1), 44100, "sample_rate_override should be applied when > 0")

func test_audio_frame_size_auto_returns_valid_value():
	var rt = ResonanceRuntimeConfig.create_default()
	rt.audio_frame_size = 0  # Auto
	var config = rt.get_config()
	var fs = config.get("audio_frame_size", -1)
	var valid = [256, 512, 1024, 2048]
	assert_true(fs in valid, "audio_frame_size Auto should return one of %s, got %s" % [valid, fs])

func test_audio_frame_size_manual_passthrough():
	var rt = ResonanceRuntimeConfig.create_default()
	rt.audio_frame_size = 1024
	var config = rt.get_config()
	assert_eq(config.get("audio_frame_size", -1), 1024, "manual audio_frame_size should pass through")

# --- get_effective_realtime_rays: pass-through on all platforms (v0.9.4+) ---
# Android no longer forces baked-only (0); realtime rays follow project settings.
# Do not reintroduce test_effective_realtime_rays_android_forces_zero — that was pre-0.9.4 behavior.

func test_effective_realtime_rays_android_passthrough():
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(2048, "Android"), 2048, "Android should pass through configured rays")
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(512, "Android"), 512, "Android should pass through low value unchanged")

func test_effective_realtime_rays_android_zero_stays_zero():
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(0, "Android"), 0, "Android 0 stays 0")

func test_effective_realtime_rays_windows_passthrough():
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(2048, "Windows"), 2048, "Windows should pass through")
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(0, "Windows"), 0, "Windows 0 stays 0")

func test_effective_realtime_rays_linux_passthrough():
	assert_eq(ResonanceRuntimeConfig.get_effective_realtime_rays(1024, "Linux"), 1024, "Linux should pass through")

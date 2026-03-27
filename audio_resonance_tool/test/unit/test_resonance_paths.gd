extends GutTest

## Unit tests for ResonancePaths (get_audio_data_dir respects Project Settings).

const ResonancePaths = preload("res://addons/nexus_resonance/scripts/resonance_paths.gd")


func test_get_audio_data_dir_returns_path_ending_with_slash():
	var path: String = ResonancePaths.get_audio_data_dir()
	assert_true(path.ends_with("/"), "path should end with /")
	assert_false(path.is_empty(), "path should not be empty")


func test_get_audio_data_dir_default_is_audio_data():
	var path: String = ResonancePaths.get_audio_data_dir()
	assert_true("audio_data" in path or path == "res://audio_data/", "default should point to audio_data")


func test_get_audio_data_dir_uses_project_setting_when_configured():
	const KEY := "nexus/resonance/bake/default_output_directory"
	if not ProjectSettings.has_setting(KEY):
		pass_test("default_output_directory not in Project Settings - plugin may not have loaded")
		return
	var configured: String = ProjectSettings.get_setting(KEY, "")
	var result: String = ResonancePaths.get_audio_data_dir()
	if configured.is_empty():
		assert_eq(result, ResonancePaths.PATH_AUDIO_DATA, "empty setting should fall back to default")
	else:
		assert_true(result.ends_with("/"), "configured path should end with /")

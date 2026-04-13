extends GutTest

## Fixture-backed tests: real [.tscn] text on disk (export / unreferenced-probe cleanup policy).

const FsPathsScript = preload("res://addons/nexus_resonance/scripts/resonance_fs_paths.gd")
const FIXTURE_SCENE_WITH_PROBE := "res://test/fixtures/scene_text_embeds_probe_path.tscn"
const PROBE_PATH_IN_FIXTURE := "res://audio_data/fixture_embedded_probe_batch.tres"


func test_fixture_tscn_file_is_readable():
	var content := FsPathsScript.read_file_as_string(FIXTURE_SCENE_WITH_PROBE)
	assert_false(content.is_empty(), "fixture must exist under res://test/fixtures/")


func test_fixture_tscn_scene_text_references_probe_path():
	var content := FsPathsScript.read_file_as_string(FIXTURE_SCENE_WITH_PROBE)
	assert_true(
		FsPathsScript.scene_text_references_probe_path(content, PROBE_PATH_IN_FIXTURE),
		"ext_resource path line in fixture should match probe needles (res + basename + absolute)"
	)

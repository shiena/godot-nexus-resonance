extends GutTest

## String-level tests for [ResonanceFsPaths] (no EditorInterface).

const FsPathsScript = preload("res://addons/nexus_resonance/scripts/resonance_fs_paths.gd")


func test_filesystem_path_for_dir_access_preserves_res_prefix_logic():
	var p := FsPathsScript.filesystem_path_for_dir_access("res://audio_data/")
	assert_false(p.begins_with("res://"), "globalized path should not stay as res://")


func test_filesystem_path_for_dir_access_empty_string():
	assert_eq(FsPathsScript.filesystem_path_for_dir_access(""), "")


func test_open_dir_for_path_empty_returns_null():
	assert_null(FsPathsScript.open_dir_for_path(""))


func test_file_exists_for_path_empty_returns_false():
	assert_false(FsPathsScript.file_exists_for_path(""))


func test_read_file_as_string_empty_returns_empty():
	assert_eq(FsPathsScript.read_file_as_string(""), "")


func test_probe_reference_needles_empty_path():
	assert_eq(FsPathsScript.probe_reference_needles_for_path("").size(), 0)


func test_probe_reference_needles_res_includes_norm_basename_and_globalized_when_differs():
	var probe := "res://audio_data/fs_paths_needle_case_baked_probes.tres"
	var needles := FsPathsScript.probe_reference_needles_for_path(probe)
	assert_gte(needles.size(), 2, "expect at least logical path and basename")
	assert_true(probe in needles, "full res path should be a needle")
	assert_true(
		"fs_paths_needle_case_baked_probes.tres" in needles,
		"basename should match tscn lines that only embed filename"
	)
	var fs_path := FsPathsScript.filesystem_path_for_dir_access(probe)
	if not fs_path.is_empty() and fs_path != probe:
		var fs_norm := fs_path.replace("\\", "/")
		assert_true(fs_norm in needles, "absolute/globalized path should be a needle")


func test_open_dir_for_path_res_root():
	var d := FsPathsScript.open_dir_for_path("res://")
	assert_not_null(d, "should open project res root")


func test_file_exists_for_path_project_godot():
	assert_true(
		FsPathsScript.file_exists_for_path("res://project.godot"),
		"project.godot should resolve via res or globalized path"
	)


func test_read_file_as_string_project_godot():
	var s := FsPathsScript.read_file_as_string("res://project.godot")
	assert_false(s.is_empty(), "project.godot should be readable as UTF-8")


func test_scene_text_references_probe_path_full_res_path():
	var probe := "res://audio_data/main_volumename_baked_probes.tres"
	var content := 'path="res://audio_data/main_volumename_baked_probes.tres"'
	assert_true(
		FsPathsScript.scene_text_references_probe_path(content, probe),
		"should match full res:// path in tscn"
	)


func test_scene_text_references_probe_path_basename_only():
	var probe := "res://audio_data/foo_bar_baked_probes.tres"
	var content := 'ExtResource path="res://audio_data/foo_bar_baked_probes.tres"'
	assert_true(
		FsPathsScript.scene_text_references_probe_path(content, probe),
		"basename needle should match when full path embedded"
	)


func test_scene_text_references_probe_path_negative():
	var probe := "res://audio_data/orphan_baked_probes.tres"
	var content := "res://audio_data/only_other_scene_baked_probes.tres"
	assert_false(
		FsPathsScript.scene_text_references_probe_path(content, probe),
		"different file basename should not match"
	)

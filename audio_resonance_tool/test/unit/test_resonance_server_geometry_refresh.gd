extends GutTest

## Regression: ResonanceServer exposes geometry refresh after scene replacement (Embree + dynamic instanced meshes).


func test_resonance_server_has_refresh_all_geometry_from_scene_tree() -> void:
	if not ClassDB.class_exists("ResonanceServer"):
		pass_test("ResonanceServer not available (GDExtension not loaded)")
		return
	assert_true(
		ClassDB.class_has_method("ResonanceServer", "refresh_all_geometry_from_scene_tree"),
		"ResonanceServer should expose refresh_all_geometry_from_scene_tree"
	)


func test_resonance_server_has_load_save_scene_data() -> void:
	if not ClassDB.class_exists("ResonanceServer"):
		pass_test("ResonanceServer not available (GDExtension not loaded)")
		return
	assert_true(ClassDB.class_has_method("ResonanceServer", "load_scene_data"))
	assert_true(ClassDB.class_has_method("ResonanceServer", "save_scene_data"))


func test_resonance_geometry_has_refresh_geometry() -> void:
	if not ClassDB.class_exists("ResonanceGeometry"):
		pass_test("ResonanceGeometry not available (GDExtension not loaded)")
		return
	assert_true(ClassDB.class_has_method("ResonanceGeometry", "refresh_geometry"))


func test_resonance_geometry_has_flush_dynamic_acoustic_transform() -> void:
	if not ClassDB.class_exists("ResonanceGeometry"):
		pass_test("ResonanceGeometry not available (GDExtension not loaded)")
		return
	assert_true(
		ClassDB.class_has_method("ResonanceGeometry", "flush_dynamic_acoustic_transform"),
		"ResonanceGeometry should expose flush_dynamic_acoustic_transform for throttled dynamic commits"
	)

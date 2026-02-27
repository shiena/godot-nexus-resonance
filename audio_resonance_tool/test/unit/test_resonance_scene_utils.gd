extends GutTest

## Unit tests for ResonanceSceneUtils (scene traversal helpers).

func test_find_resonance_static_scene_null_returns_null():
	assert_null(ResonanceSceneUtils.find_resonance_static_scene(null), "null node should return null")

func test_find_resonance_static_scene_empty_node_returns_null():
	var n = Node.new()
	var result = ResonanceSceneUtils.find_resonance_static_scene(n)
	n.free()
	assert_null(result, "node without ResonanceStaticScene should return null")

func test_collect_resonance_probe_volumes_empty():
	var n = Node.new()
	var collected: Array = []
	ResonanceSceneUtils.collect_resonance_probe_volumes(n, collected)
	n.free()
	assert_eq(collected.size(), 0, "empty tree should collect 0 volumes")

func test_collect_resonance_probe_volumes_null_safe():
	var collected: Array = []
	ResonanceSceneUtils.collect_resonance_probe_volumes(null, collected)
	assert_eq(collected.size(), 0, "null node should not crash and collect 0")

func test_collect_resonance_dynamic_geometry_empty():
	var n = Node.new()
	var collected: Array = []
	ResonanceSceneUtils.collect_resonance_dynamic_geometry(n, collected)
	n.free()
	assert_eq(collected.size(), 0, "empty tree should collect 0 dynamic geometry")

func test_collect_resonance_static_scenes_empty():
	var n = Node.new()
	var collected: Array = []
	ResonanceSceneUtils.collect_resonance_static_scenes(n, collected)
	n.free()
	assert_eq(collected.size(), 0, "empty tree should collect 0 static scenes")

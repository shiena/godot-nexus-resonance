extends Object
class_name ResonanceBakeValidation

## Editor bake prerequisites checklist and [code]audio_data[/code] writability probe.

const ResonancePaths = preload("res://addons/nexus_resonance/scripts/resonance_paths.gd")
const ResonanceFsPaths = preload("res://addons/nexus_resonance/scripts/resonance_fs_paths.gd")
const _BakeDiscovery = preload("res://addons/nexus_resonance/editor/resonance_bake_discovery.gd")


static func get_edited_scene_root(volumes: Array[Node], editor_interface: EditorInterface) -> Node:
	if editor_interface:
		var root = editor_interface.get_edited_scene_root()
		if root:
			return root
	if volumes.size() > 0:
		var n: Node = volumes[0]
		while n.get_parent():
			n = n.get_parent()
		return n
	return null


static func build_validation_checklist(
	volumes: Array[Node],
	root: Node,
	static_scene_node: Node,
	static_asset,
	resonance_runtime_script: GDScript
) -> Array:
	var gdext_ok = ResonanceServerAccess.has_server()
	var static_exported = static_scene_node != null and static_scene_node.has_valid_asset()
	var has_geometry = static_exported
	var runtime_node = (
		_BakeDiscovery.find_resonance_runtime(root, resonance_runtime_script) if root else null
	)
	var runtime_ok = runtime_node != null
	var audio_data_writable = check_audio_data_writable()
	return [
		{"label": "GDExtension loaded", "ok": gdext_ok},
		{"label": "Static scene exported (Tools > Export Static Scene)", "ok": static_exported},
		{"label": "Geometry in static asset", "ok": has_geometry},
		{"label": "ResonanceRuntime in scene", "ok": runtime_ok},
		{"label": "audio_data/ writable", "ok": audio_data_writable},
		{"label": "%d Probe Volume(s) to bake" % volumes.size(), "ok": volumes.size() > 0},
	]


static func check_audio_data_writable() -> bool:
	var audio_dir: String = ResonancePaths.get_audio_data_dir()
	var fs_dir: String = ResonanceFsPaths.filesystem_path_for_dir_access(audio_dir)
	if not DirAccess.dir_exists_absolute(fs_dir):
		var err: int = DirAccess.make_dir_recursive_absolute(fs_dir)
		if err != OK:
			return false
	var test_path: String = (
		fs_dir.path_join(".write_test_" + str(Time.get_unix_time_from_system()) + ".tmp")
	)
	var f = FileAccess.open(test_path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string("")
	f.close()
	DirAccess.remove_absolute(test_path)
	return true

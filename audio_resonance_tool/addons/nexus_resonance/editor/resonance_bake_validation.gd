extends Object
class_name ResonanceBakeValidation

## Editor bake validation helpers (edited scene root resolution).


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

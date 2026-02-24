@tool
extends ResourceFormatSaver
class_name ResonanceProbeDataSaver

## Custom ResourceFormatSaver for ResonanceProbeData (GDExtension class).
## Always saves as .tres (text format) for version control / diffing.

func _recognize(resource: Resource) -> bool:
	return resource != null and resource.get_class() == "ResonanceProbeData"

func _get_recognized_extensions(resource: Resource) -> PackedStringArray:
	return PackedStringArray(["tres"])

func _save(resource: Resource, path: String, _flags: int) -> Error:
	if resource.get_class() != "ResonanceProbeData":
		return ERR_INVALID_PARAMETER
	var data: PackedByteArray = resource.get("data")
	if data == null:
		data = PackedByteArray()
	var probe_positions: PackedVector3Array = resource.get("probe_positions") if "probe_positions" in resource else PackedVector3Array()
	var bake_params_hash: int = resource.get("bake_params_hash")
	var baked_reflection_type: int = resource.get("baked_reflection_type")
	var pathing_params_hash: int = resource.get("pathing_params_hash") if "pathing_params_hash" in resource else 0
	var static_source_params_hash: int = resource.get("static_source_params_hash") if "static_source_params_hash" in resource else 0
	var static_listener_params_hash: int = resource.get("static_listener_params_hash") if "static_listener_params_hash" in resource else 0
	var tres_path := path.get_basename() + ".tres"
	var data_str := var_to_str(data)
	var probe_pos_str := var_to_str(probe_positions)
	var content := "[gd_resource type=\"ResonanceProbeData\" format=3]\n\n[resource]\ndata = %s\nprobe_positions = %s\nbake_params_hash = %d\nbaked_reflection_type = %d\npathing_params_hash = %d\nstatic_source_params_hash = %d\nstatic_listener_params_hash = %d\n" % [data_str, probe_pos_str, bake_params_hash, baked_reflection_type, pathing_params_hash, static_source_params_hash, static_listener_params_hash]
	var f := FileAccess.open(tres_path, FileAccess.WRITE)
	if f == null:
		return FileAccess.get_open_error()
	f.store_string(content)
	f.close()
	resource.take_over_path(tres_path)
	return OK

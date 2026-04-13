extends Object
class_name ResonanceFsPaths

## Canonical filesystem paths for [DirAccess]/[FileAccess] on disk.
## [code]res://[/code] and [code]user://[/code] are globalized via [method ProjectSettings.globalize_path];
## already-absolute project paths pass through (slashes normalized).


static func filesystem_path_for_dir_access(path: String) -> String:
	if path.is_empty():
		return path
	var p := path.strip_edges().replace("\\", "/")
	if p.begins_with("res://") or p.begins_with("user://"):
		return ProjectSettings.globalize_path(p)
	return p


## Opens [DirAccess] for the given path. Tries [method filesystem_path_for_dir_access] first, then the logical path
## (same policy as listing [code]audio_data[/code] in the export handler).
static func open_dir_for_path(path: String) -> DirAccess:
	if path.is_empty():
		return null
	var fs_path := filesystem_path_for_dir_access(path)
	var d := DirAccess.open(fs_path)
	if d:
		return d
	return DirAccess.open(path)


## Returns whether a file exists at the given path ([code]res://[/code], [code]user://[/code], or absolute).
## Tries the globalized path first, then the logical path.
static func file_exists_for_path(path: String) -> bool:
	if path.is_empty():
		return false
	var fs_path := filesystem_path_for_dir_access(path)
	if FileAccess.file_exists(fs_path):
		return true
	return FileAccess.file_exists(path)


## Reads the file as a UTF-8 string. Uses the globalized path when that file exists, otherwise the logical path.
static func read_file_as_string(path: String) -> String:
	if path.is_empty():
		return ""
	var fs_path := filesystem_path_for_dir_access(path)
	if FileAccess.file_exists(fs_path):
		return FileAccess.get_file_as_string(fs_path)
	return FileAccess.get_file_as_string(path)


## Needles to detect a probe resource path inside [.tscn] text (full path, absolute, basename, [code]res://[/code] form).
static func probe_reference_needles_for_path(probe_logical_path: String) -> PackedStringArray:
	var out: PackedStringArray = []
	if probe_logical_path.is_empty():
		return out
	var norm := probe_logical_path.replace("\\", "/")
	out.append(norm)
	var fs := filesystem_path_for_dir_access(norm)
	if not fs.is_empty() and fs != norm and not out.has(fs):
		out.append(fs.replace("\\", "/"))
	var fname := norm.get_file()
	if not fname.is_empty() and not out.has(fname):
		out.append(fname)
	if not fs.is_empty():
		var localized := ProjectSettings.localize_path(fs)
		if not localized.is_empty() and not out.has(localized):
			out.append(localized)
	if norm.begins_with("res://"):
		var alt := norm
		if not out.has(alt):
			out.append(alt)
	return out


static func scene_text_references_probe_path(content: String, probe_logical_path: String) -> bool:
	for needle in probe_reference_needles_for_path(probe_logical_path):
		if needle.is_empty():
			continue
		if needle in content:
			return true
	return false

extends Node

## Nexus Resonance Logger with thematic category filtering.
## Access via Engine.get_singleton("ResonanceLogger"). Do not use class_name (conflicts with autoload).
## Use from GDScript: ResonanceLogger.log("reflections", "message", {...})
## C++ calls via Engine.get_singleton("ResonanceLogger").log(...)
##
## Categories: reflections, realtime_rays, source_volume, pathing, occlusion, init, bake, validation

const DEFAULT_BUFFER_SIZE := 128
const PROJECT_PREFIX := "audio/nexus_resonance/logger/"

## Standard categories for filtering
const CATEGORY_REFLECTIONS := &"reflections"
const CATEGORY_REALTIME_RAYS := &"realtime_rays"
const CATEGORY_SOURCE_VOLUME := &"source_volume"
const CATEGORY_PATHING := &"pathing"
const CATEGORY_OCCLUSION := &"occlusion"
const CATEGORY_INIT := &"init"
const CATEGORY_BAKE := &"bake"
const CATEGORY_VALIDATION := &"validation"

const ALL_CATEGORIES: Array[StringName] = [
	CATEGORY_REFLECTIONS,
	CATEGORY_REALTIME_RAYS,
	CATEGORY_SOURCE_VOLUME,
	CATEGORY_PATHING,
	CATEGORY_OCCLUSION,
	CATEGORY_INIT,
	CATEGORY_BAKE,
	CATEGORY_VALIDATION
]

## Emitted when a log entry is added (after category filter). Args: category, message, data
signal log_entry_added(category: StringName, message: String, data: Dictionary)

var _buffer: Array[Dictionary] = []
var _buffer_size: int = DEFAULT_BUFFER_SIZE
var _categories_enabled: Dictionary = {}
var _output_to_debug: bool = true
var _output_to_file: bool = false
var _file_path: String = "user://nexus_resonance_log.ndjson"

static var _instance: RefCounted = null

func _init() -> void:
	_load_category_defaults()
	for cat in ALL_CATEGORIES:
		if not _categories_enabled.has(cat):
			_categories_enabled[cat] = true


func _load_category_defaults() -> void:
	if not ProjectSettings.has_setting(PROJECT_PREFIX + "categories_enabled"):
		return
	var v = ProjectSettings.get_setting(PROJECT_PREFIX + "categories_enabled")
	if v is Dictionary:
		for k in v:
			_categories_enabled[StringName(str(k))] = bool(v[k])
	if ProjectSettings.has_setting(PROJECT_PREFIX + "output_to_file"):
		_output_to_file = ProjectSettings.get_setting(PROJECT_PREFIX + "output_to_file")
	if ProjectSettings.has_setting(PROJECT_PREFIX + "file_path"):
		_file_path = ProjectSettings.get_setting(PROJECT_PREFIX + "file_path")


## Log a message. Category determines filtering. Data is optional extra context.
func log(category: StringName, message: String, data: Dictionary = {}) -> void:
	if not _is_category_enabled(category):
		return

	var entry := {
		"timestamp": Time.get_ticks_msec(),
		"category": String(category),
		"message": message,
		"data": data.duplicate()
	}

	_add_to_buffer(entry)

	if _output_to_debug:
		_output_to_debug_console(category, message, data)

	if _output_to_file:
		_write_to_file(entry)

	log_entry_added.emit(category, message, data)


func _is_category_enabled(category: StringName) -> bool:
	if _categories_enabled.has(category):
		return _categories_enabled[category]
	return true


func _add_to_buffer(entry: Dictionary) -> void:
	_buffer.append(entry)
	while _buffer.size() > _buffer_size:
		_buffer.pop_front()


func _output_to_debug_console(category: StringName, message: String, data: Dictionary) -> void:
	var prefix := "[Nexus Resonance][%s] " % String(category)
	var full_msg := prefix + message
	if not data.is_empty():
		full_msg += " " + str(data)
	print(full_msg)


func _write_to_file(entry: Dictionary) -> void:
	var path := _file_path
	if path.begins_with("user://") or path.begins_with("res://"):
		path = ProjectSettings.globalize_path(path)
	var dict := {
		"timestamp": entry.timestamp,
		"category": entry.category,
		"message": entry.message,
		"data": entry.data
	}
	var line := JSON.stringify(dict) + "\n"
	var f := FileAccess.open(path, FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		push_warning("Nexus Resonance: Cannot open log file for writing: %s (error %d)" % [path, FileAccess.get_open_error()])
		return
	f.seek_end()
	f.store_string(line)
	f.close()


## Enable or disable a category. Affects future log calls.
func set_category_enabled(category: StringName, enabled: bool) -> void:
	_categories_enabled[category] = enabled


## Get whether a category is enabled
func is_category_enabled(category: StringName) -> bool:
	return _is_category_enabled(category)


## Get the last N log entries (for overlay display)
func get_recent_entries(count: int = 32) -> Array[Dictionary]:
	var start := maxi(0, _buffer.size() - count)
	var result: Array[Dictionary] = []
	for i in range(start, _buffer.size()):
		result.append(_buffer[i])
	return result


## Clear the internal buffer
func clear_buffer() -> void:
	_buffer.clear()


func set_buffer_size(size: int) -> void:
	_buffer_size = maxi(16, size)
	while _buffer.size() > _buffer_size:
		_buffer.pop_front()


func set_output_to_debug(enabled: bool) -> void:
	_output_to_debug = enabled


func set_output_to_file(enabled: bool) -> void:
	_output_to_file = enabled


func set_file_path(path: String) -> void:
	_file_path = path


## Returns all category StringNames for UI (e.g. filter checkboxes)
func get_all_categories() -> Array:
	var arr: Array = []
	for c in ALL_CATEGORIES:
		arr.append(c)
	return arr

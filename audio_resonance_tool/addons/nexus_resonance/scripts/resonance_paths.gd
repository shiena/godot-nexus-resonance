extends RefCounted
class_name ResonancePaths

## Central path constants for Nexus Resonance. Use for consistency and i18n-prep.
## Use get_audio_data_dir() for the audio output directory (respects Project Settings).

const PATH_AUDIO_DATA := "res://audio_data/"
const PATH_RESONANCE_MESHES := "res://resonance_meshes/"

const _OUTPUT_DIR_SETTING := "nexus/resonance/bake/output_dir"


## Returns the audio data output directory (bake, static export). Reads from Project Settings.
## Falls back to PATH_AUDIO_DATA if not configured. Always returns a path ending with "/".
static func get_audio_data_dir() -> String:
	if ProjectSettings.has_setting(_OUTPUT_DIR_SETTING):
		var dir: String = ProjectSettings.get_setting(_OUTPUT_DIR_SETTING, PATH_AUDIO_DATA)
		if not dir.is_empty():
			return dir if dir.ends_with("/") else dir + "/"
	return PATH_AUDIO_DATA

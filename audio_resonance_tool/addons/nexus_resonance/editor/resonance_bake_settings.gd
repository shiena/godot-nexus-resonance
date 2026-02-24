extends RefCounted
class_name ResonanceBakeSettings

## [DEPRECATED] Bake settings are now configured via ResonanceRuntimeConfig and ResonanceBakeConfig.
## This file is kept for reference only. register_bake_project_settings() is not called by the plugin.
## Do not use for new code.

const PREFIX := "audio/nexus_resonance/"

## Default values for bake parameters.
const DEFAULTS: Dictionary = {
	"bake_num_rays": 4096,
	"bake_num_bounces": 4,
	"bake_num_threads": 2,
	"bake_reflection_type": 2,
	"bake_pathing_vis_range": 500.0,
	"bake_pathing_path_range": 100.0,
	"bake_pathing_num_samples": 16,
	"bake_pathing_radius": 0.5,
	"bake_pathing_threshold": 0.1
}

## Property hints for ProjectSettings.
const HINTS: Dictionary = {
	"bake_num_rays": {"type": TYPE_INT, "hint": PROPERTY_HINT_RANGE, "hint_string": "256,16384,256"},
	"bake_num_bounces": {"type": TYPE_INT, "hint": PROPERTY_HINT_RANGE, "hint_string": "1,32,1"},
	"bake_num_threads": {"type": TYPE_INT, "hint": PROPERTY_HINT_RANGE, "hint_string": "1,64,1"},
	"bake_reflection_type": {"type": TYPE_INT, "hint": PROPERTY_HINT_ENUM, "hint_string": "Convolution:0,Parametric:1,Hybrid:2"},
	"bake_pathing_vis_range": {"type": TYPE_FLOAT, "hint": PROPERTY_HINT_RANGE, "hint_string": "10,2000,10"},
	"bake_pathing_path_range": {"type": TYPE_FLOAT, "hint": PROPERTY_HINT_RANGE, "hint_string": "10,500,10"},
	"bake_pathing_num_samples": {"type": TYPE_INT, "hint": PROPERTY_HINT_RANGE, "hint_string": "4,128,4"},
	"bake_pathing_radius": {"type": TYPE_FLOAT, "hint": PROPERTY_HINT_RANGE, "hint_string": "0.1,2.0,0.1"},
	"bake_pathing_threshold": {"type": TYPE_FLOAT, "hint": PROPERTY_HINT_RANGE, "hint_string": "0.01,1.0,0.01"}
}

## Registers bake settings in ProjectSettings if not already present.
static func register_bake_project_settings() -> void:
	for key in DEFAULTS:
		var path = PREFIX + key
		if not ProjectSettings.has_setting(path):
			ProjectSettings.set_setting(path, DEFAULTS[key])
		if key in HINTS:
			var h = HINTS[key]
			ProjectSettings.add_property_info({"name": path, "type": h.type, "hint": h.hint, "hint_string": h.hint_string})

@tool
extends Resource
class_name ResonanceBakeConfig

## Per-volume bake configuration. Link from ResonanceProbeVolume for presets.
## Saves as .tres for reusable presets. Falls back to create_default() when null.

# --- Reflection ---
@export_group("Reflection")
## Reverb algorithm for bake: Convolution (full IR), Parametric (quick), Hybrid (balanced).
@export_enum("Convolution:0", "Parametric:1", "Hybrid:2") var reflection_type: int = 2

# --- Pathing ---
@export_group("Pathing")
var _pathing_enabled: bool = false
## Enable pathing bake (multi-path sound propagation).
@export var pathing_enabled: bool:
	get:
		return _pathing_enabled
	set(v):
		if _pathing_enabled != v:
			_pathing_enabled = v
			notify_property_list_changed()
## Pathing visibility range in meters. Max distance for path finding.
@export_range(10, 2000, 10) var bake_pathing_vis_range: float = 500.0
## Pathing path range in meters. Max length of propagated paths.
@export_range(10, 500, 10) var bake_pathing_path_range: float = 100.0
## Number of pathing samples per probe. Higher = smoother, longer bake.
@export_range(4, 128, 4) var bake_pathing_num_samples: int = 16
## Sphere radius around each probe for probe-to-probe visibility tests. Larger = more samples, longer bake.
@export_range(0.1, 2.0, 0.1) var bake_pathing_radius: float = 0.5
## Pathing propagation threshold. Lower = more paths, longer bake.
@export_range(0.01, 1.0, 0.01) var bake_pathing_threshold: float = 0.1

# --- Additional Bake ---
@export_group("Additional Bake")
## Bake static source from bake_sources NodePath. Adds baked direct sound at source position.
@export var static_source_enabled: bool = false
## Bake static listener from bake_listeners NodePath. Adds baked response at listener position.
@export var static_listener_enabled: bool = false

# --- Quality ---
@export_group("Quality")
## Reflection rays per probe. Higher = better quality, longer bake.
@export_range(256, 16384, 256) var bake_num_rays: int = 4096
## Reflection bounces per ray. Higher = longer reverb tail, longer bake.
@export_range(1, 32, 1) var bake_num_bounces: int = 4
## Parallel bake threads. More = faster bake, more CPU.
@export_range(1, 64, 1) var bake_num_threads: int = 2

func _validate_property(property: Dictionary) -> void:
	if property.name in ["bake_pathing_vis_range", "bake_pathing_path_range", "bake_pathing_num_samples", "bake_pathing_radius", "bake_pathing_threshold"]:
		if not pathing_enabled:
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY

## Returns bake params dictionary for C++ set_bake_params.
func get_bake_params() -> Dictionary:
	return {
		"bake_num_rays": bake_num_rays,
		"bake_num_bounces": bake_num_bounces,
		"bake_num_threads": bake_num_threads,
		"bake_reflection_type": reflection_type,
		"bake_pathing_vis_range": bake_pathing_vis_range,
		"bake_pathing_path_range": bake_pathing_path_range,
		"bake_pathing_num_samples": bake_pathing_num_samples,
		"bake_pathing_radius": bake_pathing_radius,
		"bake_pathing_threshold": bake_pathing_threshold
	}

## Creates default bake config for volumes without one assigned.
static func create_default() -> ResonanceBakeConfig:
	return ResonanceBakeConfig.new()

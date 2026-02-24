@tool
extends Resource
class_name ResonancePlayerConfig

## Per-source configuration for ResonancePlayer. Link for reusable presets.
## Create or link in ResonancePlayer. Falls back to create_default() when null.
## Structure aligned with Steam Audio Source for compatibility.

# --- Distance / Attenuation ---
@export_group("Distance")
## Distance (meters) at which sound is at full volume. Closer than this: no attenuation.
@export_range(0.1, 100.0, 0.1) var min_distance: float = 1.0
## Max distance (meters) for attenuation. Sound reaches minimum volume at this range.
@export_range(1.0, 2000.0, 1.0) var max_distance: float = 500.0
var _attenuation_mode: int = 0
## Inverse = physics-based (1/distance). Linear = linear falloff. Curve = custom attenuation_curve.
@export_enum("Inverse:0", "Linear:1", "Curve:2") var attenuation_mode: int:
	get:
		return _attenuation_mode
	set(v):
		if _attenuation_mode != v:
			_attenuation_mode = v
			notify_property_list_changed()
## Custom attenuation curve. X = normalized distance (0..1), Y = volume. Used when attenuation_mode is Curve.
@export var attenuation_curve: Curve = null

# --- Direct Sound ---
@export_group("Direct Sound")
## Radius of the sound source in meters. Affects occlusion and diffraction.
@export_range(0.1, 10.0, 0.1) var source_radius: float = 1.0
var _air_absorption_enabled: bool = true
## Enable distance-based air absorption. Distant sounds appear muffled.
@export var air_absorption_enabled: bool:
	get:
		return _air_absorption_enabled
	set(v):
		if _air_absorption_enabled != v:
			_air_absorption_enabled = v
			notify_property_list_changed()
var _air_absorption_input: int = 0
## Air absorption source: Simulation = physics-based, User Defined = use low/mid/high sliders.
@export_enum("Simulation:0", "User Defined:1") var air_absorption_input: int:
	get:
		return _air_absorption_input
	set(v):
		if _air_absorption_input != v:
			_air_absorption_input = v
			notify_property_list_changed()
## Low-band (≤800 Hz) EQ. 0 = fully attenuated, 1 = no change. Only when air_absorption_input is User Defined.
@export_range(0.0, 1.0, 0.01) var air_absorption_low: float = 1.0
## Mid-band (800 Hz–8 kHz) EQ. 0 = fully attenuated, 1 = no change.
@export_range(0.0, 1.0, 0.01) var air_absorption_mid: float = 1.0
## High-band (≥8 kHz) EQ. 0 = fully attenuated, 1 = no change.
@export_range(0.0, 1.0, 0.01) var air_absorption_high: float = 1.0

# --- Directivity ---
@export_group("Directivity")
var _directivity_enabled: bool = false
## If enabled, the sound source becomes directional. Projects along negative Z-axis (Forward).
@export var directivity_enabled: bool:
	get:
		return _directivity_enabled
	set(v):
		if _directivity_enabled != v:
			_directivity_enabled = v
			notify_property_list_changed()
## Shape: 0 = Omnidirectional, 1 = Dipole (figure-8). Intermediate = blend.
@export_range(0.0, 1.0, 0.01) var directivity_weight: float = 0.0
## Sharpness of the directivity pattern. 0 = broad cone, 4 = narrow beam.
@export_range(0.0, 4.0, 0.1) var directivity_power: float = 1.0

# --- Output ---
@export_group("Output")
## Per-source binaural override. Use Global = follow RuntimeConfig.reverb_binaural. Disabled = panning. Enabled = force HRTF.
@export_enum("Use Global:-1", "Disabled:0", "Enabled:1") var direct_binaural_override: int = -1
## Blend between unspatialized (0) and fully 3D spatialized (1). Diegetic vs. non-diegetic mix.
@export_range(0.0, 1.0, 0.01) var spatial_blend: float = 1.0
## Encode point source to Ambisonics before binaural. For Ambisonic mix scenarios. Usually leave disabled.
@export var use_ambisonics_encode: bool = false

# --- Occlusion ---
@export_group("Occlusion")
## Number of rays per source for volumetric occlusion (1-64). Higher = smoother; lower = less CPU.
@export_range(1, 64, 1) var occlusion_samples: int = 64
## Max surfaces for sound transmission through walls (1-256). Higher = more accuracy; lower = less CPU.
@export_range(1, 256, 1) var max_transmission_surfaces: int = 32

# --- Pathing ---
@export_group("Pathing")
## Validates baked pathing data each frame. Paths checked for occlusion by dynamic geometry.
@export var path_validation_enabled: bool = true
## When a baked path is occluded, real-time path finding searches for alternate routes.
@export var find_alternate_paths: bool = true

# --- Mix Levels ---
@export_group("Mix Levels")
## Volume of the direct (line-of-sight) sound path. Range 0-10. 1.0 = nominal.
@export_range(0.0, 10.0, 0.1) var direct_mix_level: float = 1.0
## Volume of reflections and reverb. Range 0-10. 1.0 = nominal.
@export_range(0.0, 10.0, 0.1) var reflections_mix_level: float = 1.0
## Volume of pathing (multi-path propagation). Range 0-10. Requires baked pathing data.
@export_range(0.0, 10.0, 0.1) var pathing_mix_level: float = 1.0
## Pathing gain multiplier. 1.0 = full level. Range 0-10.
@export_range(0.0, 10.0, 0.1) var pathing_occ_scale: float = 1.0

# --- Hybrid Reverb ---
@export_group("Hybrid Reverb")
## Per-source EQ multiplier for low band. 1.0 = no change. Only when runtime reflection_type is Hybrid.
@export_range(0.0, 4.0, 0.1) var reflections_eq_low: float = 1.0
## Per-source EQ multiplier for mid band. 1.0 = no change.
@export_range(0.0, 4.0, 0.1) var reflections_eq_mid: float = 1.0
## Per-source EQ multiplier for high band. 1.0 = no change.
@export_range(0.0, 4.0, 0.1) var reflections_eq_high: float = 1.0
## Samples before parametric part starts. -1 = use simulation value.
@export var reflections_delay: int = -1

# --- Spatialization ---
@export_group("Spatialization")
var _perspective_correction_override: int = -1
## Per-source perspective correction. Use Global = follow RuntimeConfig. Disabled = off. Enabled = force on for this source.
@export_enum("Use Global:-1", "Disabled:0", "Enabled:1") var perspective_correction_override: int:
	get:
		return _perspective_correction_override
	set(v):
		if _perspective_correction_override != v:
			_perspective_correction_override = v
			notify_property_list_changed()
## Factor for on-screen position mapping (0.5–2.0). 1.0 = calibrated for 30–32 inch monitor. Used when Enabled; ignored when Use Global.
@export_range(0.5, 2.0, 0.1) var perspective_factor: float = 1.0

func _validate_property(property: Dictionary) -> void:
	if property.name == "perspective_factor":
		if perspective_correction_override == 0:  # Disabled
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif property.name in ["air_absorption_low", "air_absorption_mid", "air_absorption_high"]:
		if not air_absorption_enabled or air_absorption_input != 1:  # User Defined
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif property.name == "attenuation_curve":
		if attenuation_mode != 2:  # Curve
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif property.name in ["directivity_weight", "directivity_power"]:
		if not directivity_enabled:
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY

## Creates default player config for sources without one assigned.
static func create_default() -> ResonancePlayerConfig:
	return ResonancePlayerConfig.new()

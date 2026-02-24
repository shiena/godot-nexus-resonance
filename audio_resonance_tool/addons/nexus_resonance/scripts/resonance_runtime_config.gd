@tool
extends Resource
class_name ResonanceRuntimeConfig

## Nexus Resonance runtime configuration resource.
## Create or link in ResonanceRuntime node (Add Child Node > ResonanceRuntime).
## Structure aligned with Steam Audio Settings for compatibility.

signal reflection_type_changed(new_type: int)
signal pathing_enabled_changed(enabled: bool)

# --- HRTF Settings ---
@export_group("HRTF Settings")
## Enable perspective correction for spatialized sound in third-person.
@export var perspective_correction_enabled: bool = false
## Perspective correction factor. 1.0 = calibrated for 30–32 inch desktop monitor.
@export_range(0.5, 2.0, 0.1) var perspective_correction_factor: float = 1.0
## Apply binaural rendering to reverb for headphone playback.
@export var reverb_binaural: bool = true
## Use virtual surround instead of HRTF. Simpler, less accurate.
@export var use_virtual_surround: bool = false
## Custom SOFA HRTF. Import .sofa or create ResonanceSOFAAsset + load_from_file(). Null = default embedded HRTF.
@export var hrtf_sofa_asset: ResonanceSOFAAsset = null
## Bilinear HRTF interpolation. Smoother than nearest, slightly more CPU.
@export var hrtf_interpolation_bilinear: bool = false

# --- Ray Tracer Settings ---
@export_group("Ray Tracer Settings")
## Steam Audio processing block size. Match audio buffer: 256 = lower latency, more CPU. 512 = typical Godot mix. 1024 = higher latency, less CPU.
@export_enum("256:256", "512:512", "1024:1024") var audio_frame_size: int = 512
## Ambisonic order for reverb encoding. Higher = more spatial detail, more CPU.
@export_enum("1st Order:1", "2nd Order:2", "3rd Order:3") var ambisonic_order: int = 1
## Fraction of CPU cores for realtime raytracing (0–1). 0.05 = 5%, 0.5 = 50%.
@export_range(0.0, 1.0, 0.01) var simulation_cpu_cores_percent: float = 0.05
## Max reverb tail duration in seconds. Longer tails need more memory and CPU.
@export var max_reverb_duration: float = 2.0

# --- Occlusion Settings ---
@export_group("Occlusion Settings")
## Occlusion model: Raycast (thin walls) or Volumetric (thick geometry).
@export_enum("Raycast:0", "Volumetric:1") var occlusion_type: int = 1

# --- Real-time Reflections Settings ---
@export_group("Real-time Reflections Settings")
var _realtime_rays: int = 0
## Realtime raytracing rays. 0 = baked only. 64–8192 = realtime. Requires Embree on host; Android = 0.
@export_enum("Baked Only:0", "64 Rays:64", "128 Rays:128", "256 Rays:256", "512 Rays:512", "1024 Rays:1024", "2048 Rays:2048", "4096 Rays:4096", "8192 Rays:8192") var realtime_rays: int:
	get:
		return _realtime_rays
	set(v):
		if _realtime_rays != v:
			_realtime_rays = v
			notify_property_list_changed()
## Realtime reflection bounces per ray. Higher = longer reverb, more CPU.
@export_range(1, 64, 1) var realtime_bounces: int = 4
## Reverb tail length (seconds) for realtime simulation. Longer = more CPU and memory.
@export_range(0.1, 10.0, 0.1) var realtime_simulation_duration: float = 2.0
## Use Radeon Rays (GPU) instead of Embree for realtime raytracing.
@export var use_radeon_rays: bool = false
## OpenCL device type when use_radeon_rays: GPU (default), CPU, or Any. Helps when GPU has OpenCL issues.
@export_enum("GPU:0", "CPU:1", "Any:2") var opencl_device_type: int = 0
## OpenCL device index (0=first matching device). Use when multiple GPUs present.
@export_range(0, 31, 1) var opencl_device_index: int = 0
## Min distance for irradiance sampling. Lower = finer detail at close range, more CPU. Only when Realtime Rays > 0.
@export_range(0.05, 10.0, 0.01) var realtime_irradiance_min_distance: float = 0.1
## Diffuse samples per reflection point. Higher = smoother reverb, more CPU.
@export_range(8, 64, 1) var realtime_num_diffuse_samples: int = 32

# --- Baked Reverb Settings ---
@export_group("Baked Reverb Settings")
## Baked reverb influence radius in meters. Probes beyond this do not affect listener.
@export var reverb_influence_radius: float = 10000.0
## Max reverb distance (0 = unlimited). Attenuates reverb beyond this.
@export var reverb_max_distance: float = 0.0

# --- Baked Pathing Settings ---
@export_group("Baked Pathing Settings")
var _pathing_enabled: bool = false
## Enable pathing (multi-path sound propagation). Requires baked pathing in Probe Volumes.
@export var pathing_enabled: bool:
	get:
		return _pathing_enabled
	set(v):
		if _pathing_enabled != v:
			_pathing_enabled = v
			pathing_enabled_changed.emit(v)
			notify_property_list_changed()
## Pathing: normalize EQ on path effect output. Prevents pathing from sounding too bright.
@export var pathing_normalize_eq: bool = true

# --- Simulation Update Settings ---
@export_group("Simulation Update Settings")
## Direct runs every tick; Reflections+Pathing only when this many seconds elapsed. 0 = every tick (smoother pathing, more CPU). 0.1 = 100ms default.
@export_range(0.0, 1.0, 0.01) var simulation_update_interval: float = 0.1

# --- Reflection Effect Settings ---
@export_group("Reflection Effect Settings")
var _reflection_type: int = 0
## Reverb algorithm. Convolution (full IR), Parametric (fast), Hybrid (balanced), TAN (AMD GPU only – requires TrueAudio Next, falls back to Convolution otherwise).
@export_enum("Convolution:0", "Parametric:1", "Hybrid:2", "TrueAudio Next (AMD GPU):3") var reflection_type: int:
	get:
		return _reflection_type
	set(v):
		if _reflection_type != v:
			_reflection_type = v
			reflection_type_changed.emit(v)
			notify_property_list_changed()

# --- Hybrid Reverb Settings ---
@export_group("Hybrid Reverb Settings")
## Hybrid reverb: length (seconds) of IR used for convolution before parametric tail. Only when reflection_type is Hybrid.
@export_range(0.1, 2.0, 0.1) var hybrid_reverb_transition_time: float = 1.0
## Hybrid reverb: overlap percent (0–100) for crossfade between convolution and parametric parts.
@export_range(0, 100, 1) var hybrid_reverb_overlap_percent: int = 25

# --- Reverb Output ---
@export_group("Reverb Output")
## How much transmission damps reverb (0–1). 0 = no damping. 1 = full damping (reverb scaled by wall transparency). Applies to all reverb (baked and realtime).
@export_range(0.0, 1.0, 0.01) var reverb_transmission_amount: float = 1.0

# --- Debug ---
@export_group("Debug")
## Visualize reflection rays (listener origin) when Realtime Rays > 0. Requires ResonanceListener in scene.
@export var debug_reflections: bool = false
## Visualize pathing rays (source-to-listener paths). Requires pathing_enabled and baked pathing.
@export var debug_pathing: bool = false
## Enable Steam Audio context validation (extra API checks). Use for debugging. Reduces performance.
@export var context_validation: bool = false
## SIMD level cap: -1=default (AVX512), 0=AVX512, 1=AVX2, 2=AVX, 3=SSE4, 4=SSE2. Lower = more CPU compatible.
@export_range(-1, 4, 1) var context_simd_level: int = -1

# --- Physics ---
@export_group("Physics")
## Transmission model: FreqIndependent (faster) or FreqDependent (realistic through materials).
@export_enum("FreqIndependent:0", "FreqDependent:1") var transmission_type: int = 1
## Throttle dynamic geometry transform updates: apply scene commit every Nth update only (1=every frame, 4=every 4th). Reduces CPU and audio stutter with animated objects.
@export_range(1, 16, 1) var geometry_update_throttle: int = 4
## Throttle simulation tick: run audio simulation every Nth frame only (1=every frame, 2=every 2nd). Reduces late count.
@export_range(1, 8, 1) var simulation_tick_throttle: int = 1

func _validate_property(property: Dictionary) -> void:
	if property.name in ["hybrid_reverb_transition_time", "hybrid_reverb_overlap_percent"]:
		if reflection_type != 2:  # Hybrid
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif property.name in ["opencl_device_type", "opencl_device_index"]:
		if not use_radeon_rays and reflection_type != 3:  # TAN also needs OpenCL
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif property.name in ["realtime_irradiance_min_distance", "realtime_num_diffuse_samples", "realtime_bounces", "realtime_simulation_duration", "use_radeon_rays"]:
		if realtime_rays == 0:
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif property.name == "pathing_normalize_eq":
		if not pathing_enabled:
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY

## Returns effective realtime_rays for the given OS. Android has no Embree, so always 0.
static func get_effective_realtime_rays(realtime_rays: int, os_name: String) -> int:
	if os_name == "Android" and realtime_rays > 0:
		return 0
	return realtime_rays

## Returns config dictionary for init_audio_engine. Includes sample_rate from AudioServer.
func get_config() -> Dictionary:
	var rays := get_effective_realtime_rays(realtime_rays, OS.get_name())
	return {
		"sample_rate": int(AudioServer.get_mix_rate()),
		"audio_frame_size": audio_frame_size,
		"ambisonic_order": ambisonic_order,
		"simulation_cpu_cores_percent": simulation_cpu_cores_percent,
		"max_reverb_duration": max_reverb_duration,
		"realtime_rays": rays,
		"realtime_bounces": realtime_bounces,
		"use_radeon_rays": use_radeon_rays,
		"opencl_device_type": opencl_device_type,
		"opencl_device_index": opencl_device_index,
		"realtime_irradiance_min_distance": realtime_irradiance_min_distance,
		"realtime_simulation_duration": realtime_simulation_duration,
		"realtime_num_diffuse_samples": realtime_num_diffuse_samples,
		"reflection_type": reflection_type,
		"hybrid_reverb_transition_time": hybrid_reverb_transition_time,
		"hybrid_reverb_overlap_percent": hybrid_reverb_overlap_percent,
		"reverb_binaural": reverb_binaural,
		"use_virtual_surround": use_virtual_surround,
		"hrtf_sofa_asset": hrtf_sofa_asset,
		"hrtf_interpolation_bilinear": hrtf_interpolation_bilinear,
		"reverb_influence_radius": reverb_influence_radius,
		"reverb_max_distance": reverb_max_distance,
		"reverb_transmission_amount": reverb_transmission_amount,
		"pathing_enabled": pathing_enabled,
		"pathing_normalize_eq": pathing_normalize_eq,
		"transmission_type": transmission_type,
		"occlusion_type": occlusion_type,
		"geometry_update_throttle": geometry_update_throttle,
		"simulation_tick_throttle": simulation_tick_throttle,
		"simulation_update_interval": simulation_update_interval,
		"perspective_correction_enabled": perspective_correction_enabled,
		"perspective_correction_factor": perspective_correction_factor,
		"debug_reflections": debug_reflections,
		"debug_pathing": debug_pathing,
		"context_validation": context_validation,
		"context_simd_level": context_simd_level,
		"output_direct": true,
		"output_reverb": true
	}

## Creates a default runtime config for editor/fallback when no ResonanceRuntime in scene.
static func create_default() -> ResonanceRuntimeConfig:
	return ResonanceRuntimeConfig.new()

@tool
extends Resource
class_name ResonanceRuntimeConfig

## Nexus Resonance runtime configuration resource.
## Create or link in ResonanceRuntime node (Add Child Node → **ResonanceRuntime** global class).
## Structure aligned with Steam Audio Settings for compatibility.

const Constants = preload("resonance_config_constants.gd")

signal reflection_type_changed(new_type: int)
signal pathing_enabled_changed(enabled: bool)
signal audio_frame_size_changed(new_size: int)

# --- Audio ---
@export_group("Audio")
## Sample rate override. Use Godot Mix Rate = follow project settings. Other rates override; mismatch may cause audio issues (no resampling).
@export_enum(
	"Use Godot Mix Rate:0",
	"22050 Hz:22050",
	"44100 Hz:44100",
	"48000 Hz:48000",
	"96000 Hz:96000",
	"192000 Hz:192000"
)
var sample_rate_override: int = 0
## Steam Audio processing block size. Auto = derive from Project Settings (audio/driver/output_latency). Manual = override for performance tuning.
var _audio_frame_size: int = 0
@export_enum("Auto:0", "256:256", "512:512", "1024:1024", "2048:2048")
var audio_frame_size: int:
	get:
		return _audio_frame_size
	set(v):
		if _audio_frame_size != v:
			_audio_frame_size = v
			audio_frame_size_changed.emit(v)
## Target bus for Direct + Pathing (player output). Empty = Master. Fallback when players use Global bus override.
@export var bus: StringName = &"Master"
## Bus for convolution reverb (effect and activator). Empty = ResonanceReverb.
## Reverb output is sent to [member bus] (same as Direct+Pathing). No separate send bus.
@export var reverb_bus_name: StringName = &"ResonanceReverb"

# --- HRTF & Spatialization ---
@export_group("HRTF & Spatialization")
## Enable perspective correction for spatialized sound in third-person.
@export var perspective_correction_enabled: bool = false
## Perspective correction factor. 1.0 = calibrated for 30–32 inch desktop monitor.
@export_range(0.5, 2.0, 0.1) var perspective_correction_factor: float = 1.0
## Apply binaural rendering to reverb for headphone playback.
@export var reverb_binaural: bool = true
## Use virtual surround instead of HRTF. Simpler, less accurate.
@export var use_virtual_surround: bool = false
## Direct path without HRTF: speaker layout for IPLPanningEffect (and optional Ambisonics→speaker decode for surround). Godot player output stays stereo (fold-down). Use 1/2/4/6/8 only; other values become stereo at runtime.
@export_enum("Mono:1", "Stereo:2", "Quad:4", "5.1:6", "7.1:8") var direct_speaker_channels: int = 2
## HRTF volume gain (dB) for the embedded default HRTF. With custom SOFA, added to each asset's [member ResonanceSOFAAsset.volume_db] (linear gain product).
@export_range(-24.0, 24.0, 0.1) var hrtf_volume_db: float = 0.0
## HRTF normalization for the embedded default HRTF only (None / RMS). Custom SOFA files use [member ResonanceSOFAAsset.norm_type] on the asset.
@export_enum("None:0", "RMS:1") var hrtf_normalization_type: int = 0
## SOFA HRTF library (Unity-style). [member hrtf_sofa_selected_index] picks the active entry. Empty = default embedded HRTF.
@export var hrtf_sofa_assets: Array[ResonanceSOFAAsset] = []
## Index into [member hrtf_sofa_assets] for the active SOFA (clamped at runtime).
@export_range(0, 64, 1) var hrtf_sofa_selected_index: int = 0
## Bilinear HRTF interpolation. Smoother than nearest, slightly more CPU.
@export var hrtf_interpolation_bilinear: bool = false

# --- Reflections ---
@export_group("Reflections")
## When sources use "Use Global" reflections_type: Baked = probe interpolation; Realtime = raytracing (requires realtime_rays > 0).
@export_enum("Baked:0", "Realtime:1") var default_reflections_mode: int = 0
var _reflection_type: int = Constants.REFLECTION_TYPE_CONVOLUTION
## Reverb algorithm. Parametric (fastest). Convolution uses ReflectionMixer (bundled convolutions).
## Hybrid = convolution + parametric tail (no mixer; can be slower than Convolution – reduce hybrid_reverb_transition_time and ambisonic_order for better perf).
## TrueAudio Next: Steam Audio supports TAN on 64-bit Windows only; other platforms fall back to Convolution with a warning.
@export_enum("Convolution:0", "Parametric:1", "Hybrid:2", "TrueAudio Next (AMD GPU):3")
var reflection_type: int:
	get:
		return _reflection_type
	set(v):
		if _reflection_type != v:
			_reflection_type = v
			reflection_type_changed.emit(v)
			notify_property_list_changed()
## Upper bound for simulator / ReflectionMixer IR allocation (s): caps [code]simulation_settings.maxDuration[/code] and bundled convolution IR size. Not the per-run simulation length (see [member realtime_simulation_duration]). Longer values need more memory and CPU.
@export_range(0.1, 10.0, 0.1) var max_reverb_duration: float = 2.0
var _realtime_rays: int = 0
## Realtime raytracing rays. 0 = baked-only reflections/reverb (fastest for parametric/conv from probes). 64–8192 = realtime simulation (uses [member scene_type]; on platforms without Embree/OpenCL the native layer falls back to Default tracer).
## High ray counts dominate CPU; match Unity „Real Time Rays“ — lower or 0 when using baked parametric + pathing.
@export_enum(
	"Baked Only:0",
	"64 Rays:64",
	"128 Rays:128",
	"256 Rays:256",
	"512 Rays:512",
	"1024 Rays:1024",
	"2048 Rays:2048",
	"4096 Rays:4096",
	"8192 Rays:8192"
)
var realtime_rays: int:
	get:
		return _realtime_rays
	set(v):
		if _realtime_rays != v:
			_realtime_rays = v
			notify_property_list_changed()
## Realtime reflection bounces per ray. Higher = longer reverb, more CPU.
@export_range(1, 64, 1) var realtime_bounces: int = 4
## Impulse response length (s) passed to real-time simulation shared inputs ([code]IPLSimulationSharedInputs.duration[/code]) each run. Distinct from [member max_reverb_duration] (allocator / mixer IR cap). Longer = more CPU and memory.
@export_range(0.1, 10.0, 0.1) var realtime_simulation_duration: float = 2.0
## Hybrid reverb: length (seconds) of IR used for convolution before parametric tail. Lower = less CPU (e.g. 0.2–0.3 s for better hybrid performance). Only when reflection_type is Hybrid.
@export_range(0.1, 2.0, 0.1) var hybrid_reverb_transition_time: float = 1.0
## Hybrid reverb: overlap percent (0–100) for crossfade between convolution and parametric parts.
@export_range(0, 100, 1) var hybrid_reverb_overlap_percent: int = 25

# --- Reverb & Pathing ---
@export_group("Reverb & Pathing")
## Probes beyond this radius (m) do not affect listener. Similar to Unity Baked Source Influence Radius.
@export var reverb_influence_radius: float = 10000.0
## Extra reverb attenuation beyond this distance (m). 0 = use attenuation only. Unity: no direct equivalent.
@export var reverb_max_distance: float = 0.0
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
## Runtime pathing: Steam Audio [code]numVisSamples[/code] (probe visibility rays per pathing update). 1–16. Lower = less CPU; higher ≈ closer to bake density ([ResonanceBakeConfig] [code]bake_pathing_num_samples[/code] is bake-only). With path validation / alternate paths on, this dominates pathing cost (Embree or Default tracer).
@export_range(1, 16, 1) var pathing_num_vis_samples: int = 4
## Default when a [ResonancePlayer] uses **Use Global** for path validation: re-check baked paths for occlusion by dynamic geometry each update (higher CPU).
@export var path_validation_enabled: bool = true
## Default when a player uses **Use Global** for find alternate paths: search realtime routes when a baked path is occluded (very CPU-heavy; only effective if validation is on).
@export var find_alternate_paths: bool = false
## How much transmission damps reverb (0–1). 0 = no damping. 1 = full damping (reverb scaled by wall transparency). Applies to all reverb (baked and realtime).
@export_range(0.0, 1.0, 0.01) var reverb_transmission_amount: float = 1.0

# --- Occlusion & Physics ---
@export_group("Occlusion & Physics")
## Collision mask for Godot [code]PhysicsRayQueryParameters3D[/code] when [member scene_type] is Custom (Godot Physics). [code]-1[/code] = all physics layers.
@export_flags_3d_physics var physics_ray_collision_mask: int = -1
## Occlusion model: Raycast (binary hit) or Volumetric (fractional occlusion from samples; Steam Audio [code]numOcclusionSamples[/code]). Volumetric only affects how occlusion is computed, not how transmission coefficients are banded. Pair with [member ResonancePlayerConfig.occlusion_samples] and [member ResonancePlayerConfig.source_radius] on each source.
@export_enum("Raycast:0", "Volumetric:1") var occlusion_type: int = 1
## Simulator cap for volumetric occlusion samples (Steam Audio [code]maxNumOcclusionSamples[/code]). Per-source [member ResonancePlayerConfig.occlusion_samples] are clamped to this.
@export_range(1, 128, 1) var max_occlusion_samples: int = 64
## Maximum simultaneous sources for realtime reflection simulation (Steam Audio [code]maxNumSources[/code]). Higher values use more CPU and memory.
@export_range(8, 128, 1) var max_simulation_sources: int = 32
## Direct-effect frequency mode for transmission (Steam Audio [code]IPLTransmissionType[/code] on the direct processor): FreqIndependent (one blended coefficient) or FreqDependent (low/mid/high). This does not add “softer” material boundaries or a volumetric transmission path; Steam Audio [code]IPLSimulationInputs[/code] exposes [code]numTransmissionRays[/code] for path depth only, not an occlusion-style Raycast/Volumetric switch for transmission.
@export_enum("FreqIndependent:0", "FreqDependent:1") var transmission_type: int = 1
## Throttle dynamic geometry transform updates: apply scene commit every Nth update only (1=every frame, 4=every 4th). Reduces CPU and audio stutter with animated objects.
@export_range(1, 16, 1) var geometry_update_throttle: int = 4
## Throttle simulation worker wake: every Nth frame tick from ResonanceRuntime (1 = every frame).
## Higher values reduce CPU from [code]iplSimulatorRunDirect[/code] and cache sync; occlusion/listener react slower.
## Unity analogue: fewer simulation thread wakeups when combined with a reasonable [member simulation_update_interval].
@export_range(1, 8, 1) var simulation_tick_throttle: int = 1
## Minimum seconds between [code]iplSimulatorRunDirect[/code] on worker ticks that do not run reflections/pathing.
## 0 = run direct every worker wake (legacy). 0.02–0.05 reduces direct/occlusion cost when [member simulation_tick_throttle] is 1 and [member simulation_update_interval] is high.
@export_range(0.0, 1.0, 0.005) var direct_sim_interval: float = 0.0
## When on, players enqueue source updates and ResonanceRuntime applies them once per frame after [method ResonanceServer.tick] (one [code]simulation_mutex[/code] pass). When off, each player uses try-lock per frame.
@export var batch_source_updates: bool = true

# --- Advanced ---
@export_group("Advanced")
var _scene_type: int = 0
## Ray tracer backend. Default = built-in Phonon; Embree = Intel (faster CPU on Windows/Linux/macOS per Steam Audio).
## Radeon Rays = OpenCL GPU path supported on 64-bit Windows only; Linux/macOS/Android/iOS fall back to Default with a warning.
## For realtime rays with reflections/pathing, prefer Embree (or Radeon Rays on Windows) — Unity docs warn the slowest tracer is unsuitable for heavy simulation.
## Godot often omits `scene_type = 0` in .tres; initializer 0 keeps „Default“ after reload. Embree/Radeon/Custom serialize as 1/2/3.
## Custom = Steam Audio [code]IPL_SCENETYPE_CUSTOM[/code]: raycasts use Godot 3D physics (see [member physics_ray_collision_mask]). Simulation runs on the main thread during the physics frame ([code]_physics_process[/code] in **ResonanceRuntime**) so [code]PhysicsDirectSpaceState3D[/code] is valid when **Project Settings → Physics → 3D → Run on Separate Thread** is on. ResonanceGeometry meshes are not uploaded to Phonon.
@export_enum("Default:0", "Embree:1", "Radeon Rays:2", "Custom (Godot Physics):3") var scene_type: int:
	get:
		return _scene_type
	set(v):
		if _scene_type != v:
			_scene_type = v
			notify_property_list_changed()
## OpenCL device type when scene_type is Radeon Rays: GPU (default), CPU, or Any. Helps when GPU has OpenCL issues.
@export_enum("GPU:0", "CPU:1", "Any:2") var opencl_device_type: int = 0
## OpenCL device index (0=first matching device). Use when multiple GPUs present.
@export_range(0, 31, 1) var opencl_device_index: int = 0
## Min distance for irradiance sampling. Lower = finer detail at close range, more CPU. Only when Realtime Rays > 0.
@export_range(0.05, 10.0, 0.01) var realtime_irradiance_min_distance: float = 0.1
## Diffuse samples per reflection point. Higher = smoother reverb, more CPU.
@export_range(8, 64, 1) var realtime_num_diffuse_samples: int = 32
## Ambisonic order for reverb encoding. Higher = more spatial detail, more CPU.
@export_enum("1st Order:1", "2nd Order:2", "3rd Order:3") var ambisonic_order: int = 1
## Fraction of CPU cores for Steam Audio simulation threads (0–1). 0.15 ≈ 15% of logical cores; raise for heavier realtime reflections/pathing (Unity: CPU %).
@export_range(0.0, 1.0, 0.01) var simulation_cpu_cores_percent: float = 0.15
## SIMD level cap: -1=default (AVX512), 0=AVX512, 1=AVX2, 2=AVX, 3=SSE4, 4=SSE2. Lower = more CPU compatible.
@export_range(-1, 4, 1) var context_simd_level: int = -1
## Enable Steam Audio context validation ([code]IPL_CONTEXTFLAGS_VALIDATION[/code]). Extra API checks; may log warnings during simulation (debugging only).
@export var context_validation: bool = false
## Minimum seconds between [code]RunReflections[/code] and [code]RunPathing[/code] on the simulation worker (Steam Audio „Simulation Update Interval“ in Unity).
## Direct simulation still runs each worker wake. 0 = run heavy sim every worker tick (highest CPU). 0.1 = 100 ms default; 0.15–0.25 = cheaper pathing/reflection follow. Increase when using path validation + alternate paths with Embree to cap pathing frequency.
@export_range(0.0, 1.0, 0.01) var simulation_update_interval: float = 0.1


func _validate_property(property: Dictionary) -> void:
	if property.name in ["hybrid_reverb_transition_time", "hybrid_reverb_overlap_percent"]:
		if reflection_type != Constants.REFLECTION_TYPE_HYBRID:
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif property.name in ["opencl_device_type", "opencl_device_index"]:
		if scene_type != 2 and reflection_type != Constants.REFLECTION_TYPE_TAN:
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif (
		property.name
		in [
			"realtime_irradiance_min_distance",
			"realtime_num_diffuse_samples",
			"realtime_bounces",
			"realtime_simulation_duration"
		]
	):
		if realtime_rays == 0:
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY
	elif property.name in ["pathing_normalize_eq", "pathing_num_vis_samples", "path_validation_enabled", "find_alternate_paths"]:
		if not pathing_enabled:
			property["usage"] = property["usage"] | PROPERTY_USAGE_READ_ONLY


## Returns effective bus for Direct + Pathing. Empty config = Master.
func get_bus_effective() -> StringName:
	return bus if not str(bus).is_empty() else &"Master"


## Returns effective reverb bus name. Empty config = ResonanceReverb.
func get_reverb_bus_name_effective() -> StringName:
	return reverb_bus_name if not str(reverb_bus_name).is_empty() else &"ResonanceReverb"


func _sofa_asset_data_nonempty(asset: ResonanceSOFAAsset) -> bool:
	return asset != null and not asset.get_sofa_data().is_empty()


## Active custom SOFA for init: [member hrtf_sofa_assets] at [member hrtf_sofa_selected_index], or first list entry with data if that slot is empty. Null if the list is empty or has no valid SOFA data.
func get_hrtf_sofa_effective() -> ResonanceSOFAAsset:
	if hrtf_sofa_assets.is_empty():
		return null
	var idx := clampi(hrtf_sofa_selected_index, 0, hrtf_sofa_assets.size() - 1)
	var picked: ResonanceSOFAAsset = hrtf_sofa_assets[idx]
	if _sofa_asset_data_nonempty(picked):
		return picked
	for i in hrtf_sofa_assets.size():
		var a: ResonanceSOFAAsset = hrtf_sofa_assets[i]
		if _sofa_asset_data_nonempty(a):
			return a
	return null


## Returns realtime_rays unchanged for all platforms. [param os_name] is reserved for future per-OS caps; callers should pass [method OS.get_name].
static func get_effective_realtime_rays(realtime_rays: int, os_name: String) -> int:
	return realtime_rays


## Derives Godot mix buffer size from Project Settings (audio/driver/output_latency). Matches reverb bus frame_count.
static func _get_audio_frame_size_from_project() -> int:
	var lat_ms: float = 15.0
	if ProjectSettings.has_setting("audio/driver/output_latency"):
		var lat_var = ProjectSettings.get_setting("audio/driver/output_latency")
		if lat_var is float:
			lat_ms = lat_var
		elif lat_var is int:
			lat_ms = float(lat_var)
	var mix_rate := int(AudioServer.get_mix_rate())
	var raw := int(lat_ms * mix_rate / 1000.0)
	# Closest of 256, 512, 1024, 2048 to match Godot's mix buffer
	var candidates := [256, 512, 1024, 2048]
	var best := 512
	var best_dist := 999999
	for c in candidates:
		var d := abs(raw - c)
		if d < best_dist:
			best_dist = d
			best = c
	return best


## Returns config dictionary for [method ResonanceServer.init_audio_engine]. Includes sample_rate from AudioServer or sample_rate_override. Does not include [member bus] / [member reverb_bus_name]; Godot bus routing is applied in [ResonanceRuntime] only.
func get_config() -> Dictionary:
	var rays := get_effective_realtime_rays(realtime_rays, OS.get_name())
	var mix_rate := int(AudioServer.get_mix_rate())
	var rate := sample_rate_override if sample_rate_override > 0 else mix_rate
	if sample_rate_override > 0 and sample_rate_override != mix_rate:
		push_warning(
			(
				"Nexus Resonance: sample_rate_override (%d) differs from Godot mix rate (%d). No resampling; audio may be affected."
				% [sample_rate_override, mix_rate]
			)
		)
	var frame_size := (
		audio_frame_size if audio_frame_size > 0 else _get_audio_frame_size_from_project()
	)
	return {
		"sample_rate": rate,
		"audio_frame_size": frame_size,
		"audio_frame_size_was_auto": audio_frame_size == 0,
		"ambisonic_order": ambisonic_order,
		"simulation_cpu_cores_percent": simulation_cpu_cores_percent,
		"max_reverb_duration": max_reverb_duration,
		"realtime_rays": rays,
		"realtime_bounces": realtime_bounces,
		"scene_type": scene_type,
		"physics_ray_collision_mask": physics_ray_collision_mask,
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
		"direct_speaker_channels": direct_speaker_channels,
		"hrtf_volume_db": hrtf_volume_db,
		"hrtf_normalization_type": hrtf_normalization_type,
		"hrtf_sofa_asset": get_hrtf_sofa_effective(),
		"hrtf_interpolation_bilinear": hrtf_interpolation_bilinear,
		"reverb_influence_radius": reverb_influence_radius,
		"reverb_max_distance": reverb_max_distance,
		"reverb_transmission_amount": reverb_transmission_amount,
		"pathing_enabled": pathing_enabled,
		"pathing_normalize_eq": pathing_normalize_eq,
		"pathing_num_vis_samples": pathing_num_vis_samples,
		"path_validation_enabled": path_validation_enabled,
		"find_alternate_paths": find_alternate_paths,
		"transmission_type": transmission_type,
		"occlusion_type": occlusion_type,
		"max_occlusion_samples": max_occlusion_samples,
		"max_simulation_sources": max_simulation_sources,
		"geometry_update_throttle": geometry_update_throttle,
		"simulation_tick_throttle": simulation_tick_throttle,
		"simulation_update_interval": simulation_update_interval,
		"direct_sim_interval": direct_sim_interval,
		"batch_source_updates": batch_source_updates,
		"perspective_correction_enabled": perspective_correction_enabled,
		"perspective_correction_factor": perspective_correction_factor,
		"context_simd_level": context_simd_level,
		"context_validation": context_validation,
		"default_reflections_mode": default_reflections_mode,
		"output_direct": true,
		"output_reverb": true
	}


## Creates a default runtime config for editor/fallback when no ResonanceRuntime in scene.
static func create_default() -> ResonanceRuntimeConfig:
	return ResonanceRuntimeConfig.new()

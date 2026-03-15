# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.1] - 2026-03-15

**Stability improvements**

### Fixed

- **Memory leak on runtime reinit** - `discard_meshes_before_scene_release` now correctly calls `iplStaticMeshRemove`/`iplStaticMeshRelease` and `iplInstancedMeshRemove`/`iplInstancedMeshRelease` before clearing handles. Previously, IPL resources were leaked when changing reflection type or pathing (triggering `reinit_audio_engine`). Phonon uses refcounting; meshes must be explicitly released per Steam Audio API.

### Changed

- **Defensive null checks** - `ResonanceProbeBatchRegistry`: guards for `sim_mutex` before `std::lock_guard` to avoid crash when null. `parse_mesh_to_ipl`: early return when `mesh.is_null()`. `Engine::get_singleton()`: null checks in `ResonanceGeometry` and `ResonanceProbeVolume` before `is_editor_hint()`.
- ** region_size validation** - `ResonanceProbeVolume.set_region_size` clamps each component to minimum `kProbeRegionSizeMin` (0.1) to prevent degenerate volumes.
- **Empty asset guard** - `add_static_scene_from_asset` rejects assets with `get_size() == 0` before passing to Phonon.

## [0.9.0] - 2026-03-14

### Added

- **Performance Overlay** - Optional overlay (FPS, frame time, physics time). Enable via `performance_overlay_enabled` on ResonanceRuntime; toggle with F4. Independent from debug overlay.
- **ResonanceLogger startup log** - Init entry "ResonanceLogger ready" when logger starts. Log panel opens expanded by default.
- **Improved empty log message** - Explains when logs appear (init, bake, validation, etc.).

### Removed

- **debug_overlay_visible** - Debug overlay now only toggles with F3; no export for initial visibility.
- **debug_reflections** (ResonanceRuntimeConfig) - Merged into `debug_sources` on ResonanceRuntime; one switch controls both occlusion and reflection ray viz.
- **debug_pathing** (ResonanceRuntimeConfig) - Removed.
- **context_validation** (ResonanceRuntimeConfig) - Removed.

## [0.8.8] - 2026-03-10

**Stability and Consistency Update**

### Added

- **InitFlags for ResonanceDirectProcessor** - Bitwise `DirectInitFlags` (DIRECT_EFFECT, BINAURAL_EFFECT, PANNING_EFFECT, BUFFERS, AMBISONICS_ENCODE). `process()` only runs when all required flags are set; avoids partial-init crashes.
- **InitFlags for ResonanceAmbisonicProcessor** - Bitwise `AmbisonicInitFlags` (ROTATION, DECODE, BUFFERS). Aligns with Mixer/Reflection/Path processor consistency.
- **Input-start detection** - `ResonanceInternalPlayback` delays processing until first non-zero input sample. Avoids ramp artifacts when Godot sends incorrect params before playback actually starts.
- **Process-entry guards** - Centralised null checks at start of `_process_steam_audio_block`: context, srv, buffers (sa_in_buffer, sa_direct_out_buffer, sa_path_out_buffer, sa_final_mix_buffer). Early return on invalid state.
- **Passthrough fallback on init failure** - When Direct or Ambisonic processor init fails, pass input through instead of silence. Direct: copy in_buffer to out_buffer. Ambisonic: decode W (omnidirectional) channel to stereo (1/√2). Reflection/Path add to mix; when they fail, dry from Direct is unchanged.
- **HRTF and ReflectionMixer double-buffer** - main/init writes to buffer [1], audio thread reads from [0] via `get_hrtf()` and `get_reflection_mixer_handle()`; atomic flags trigger swap on consume. Prepares for future SOFA hot-reload or config changes.
- **Processor pattern documentation** - DEVELOPERS.md: init order, release order, InitFlags usage, process guards, double-buffering. Note on Steam Audio mix_return_effect buffer bug to avoid when writing lazy-init code.
- **Debug overlay toggle** - `debug_overlay_toggle_key` (default F3) toggles the debug overlay at runtime. When overlay is on: cursor visible, camera ignores mouse input. Restores previous mouse mode when overlay is closed.
- **Reverb Bus Crackling Debug** - Debug overlay section shows `fetch_reverb` lock_ok, cache_hit, cache_miss to diagnose audio dropouts and crackling.

### Fixed

- **Crackling with Ambisonic Order 2/3 and Convolution/Hybrid** - Audio thread no longer blocks on `simulation_mutex`. Non-blocking `try_lock` plus last-known-good cache (like Parametric) for Convolution/Hybrid/TAN. When the worker holds the lock, the audio thread uses cached reflection params instead of blocking → avoids Xruns and crackling.
- **Pathing crackling** - `fetch_pathing_params` now uses `try_lock` plus per-source pathing cache (`CachedPathingParams` with eqCoeffs and SH coefficient copy). When the worker holds `simulation_mutex` during RunPathing, the audio thread uses cached pathing params instead of blocking. Cache invalidated on source/batch removal and shutdown.
- **Reflections despite occlusion** - Transmission is no longer applied to reverb. Reflections are indirect paths that go around obstacles; only the direct path uses transmission. Fixes missing reflections when the listener has no line of sight to the source.

### Changed

- **Double-buffer for audio thread** - Listener coordinates and parametric reverb cache use lock-free double-buffering. Main/worker write to buffer [1], audio reads from [0]; atomic flags trigger swap on consume. Reduces lock contention in the audio hot path (`get_current_listener_coords`, `fetch_reverb_params` parametric fallback).
- **Reflection cache** - Convolution/Hybrid/TAN reflection params are cached per-source when `fetch_reverb_params` succeeds. Cache invalidated on source/batch removal and shutdown.
- **Pathing cache** - Pathing params (eqCoeffs, sh_coeffs) cached per-source when `fetch_pathing_params` succeeds. Double-buffer swap on consume. Invalidated on source removal, probe batch removal, revalidate, clear batches, and shutdown.
- **kMaxBlocksPerMixCall** - Increased from 2 to 4 to better handle Godot frames=1024 with frame_size=512 (more blocks per mix callback).
- **GDExtension unload safety** - `clear_probe_batches` in plugin `_disable_plugin` only runs when `Engine.has_singleton("ResonanceServer")` is true. Avoids crash when editor closes and GDExtension unloads before the plugin.
- **Probe Volume deletion robustness** - ResonancePlayer auto-clears `pathing_probe_volume` when target node is gone; ResonanceProbeVolume `_clear_player_refs_to_this` falls back to tree root when edited scene root is null (e.g. editor teardown).

### Documentation

- **simulation_update_interval** - ResonanceRuntimeConfig: Order 2/3 + Convolution/Hybrid: recommend ≥ 0.2 s if crackling persists.
- **Audio-Buffer-and-Latency** - Added `simulation_update_interval` tip for crackling workaround.

## [0.8.7] - 2026-03-07

**Reliability Update**

### Added

- **Pre-bake audio_data writability check** - Validation checklist now includes "audio_data/ writable" to fail fast when probe data cannot be saved.
- **Unit test for invalid probe data** - Edge case test for malformed data field in ResonanceProbeDataLoader.
- **ResonanceUtils::safe_unit_vector** - Defensive vector normalization with minimum length guard (1e-3) and fallback to avoid NaN/division-by-zero from degenerate transforms.

### Fixed

- **IPL error handling** - All `ipl*Create` calls (iplSceneCreate, iplSimulatorCreate, iplReflectionMixerCreate, iplSerializedObjectCreate, iplProbeBatchCreate) now check `IPL_STATUS_SUCCESS`, log via ResonanceLog, and perform cleanup on failure. Prevents crashes when Steam Audio init or bake fails.
- **Atomic probe data saves** - ResonanceProbeDataSaver writes to `.tmp` then renames atomically to avoid corrupted files on crash or power loss.
- **DirAccess.make_dir failure handling** - Export and bake now check mkdir result and show error when audio_data cannot be created.
- **Defensive vector normalization** - `update_listener` and `_update_source_internal` use `safe_unit_vector` for dir/up/right orthonormalization; prevents degenerated coordinate spaces when inputs have near-zero length.
- **Shutdown atomic flags reset** - `_shutdown_steam_audio` resets pending_listener_valid, simulation_requested, reflections_pathing_requested, scene_dirty, pathing_ran_this_tick at start to avoid late accesses during/after shutdown.

### Changed

- **ResonanceServer init** - `_init_scene_and_simulator` returns bool; on IPL failure cleans up and resets context so is_initialized() stays false.
- **Probe data loader** - Length limits on data/probe_positions expressions to avoid excessive memory use from malformed .tres files.
- **Config clamping** - `ambisonic_order` clamped to 1-3, `max_reverb_duration` to 0.1-10.0 s in ResonanceServerConfig; `max_reverb_duration` export_range in ResonanceRuntimeConfig.
- **Processor InitFlags** - ResonanceMixerProcessor, ResonanceReflectionProcessor, ResonancePathProcessor use bitwise InitFlags; process() guards ensure only fully initialized combinations run (avoids partial init crashes).

## [0.8.6] - 2026-03-04

### Added

- **Ray tracer selection** - `ResonanceRuntimeConfig.scene_type`: Default (0) = built-in Phonon; Embree (1) = Intel, faster CPU; Radeon Rays (2) = GPU. Developers can explicitly choose Default for maximum compatibility or Embree for better CPU performance.
- **Listener notification API** - `ResonanceServer.notify_listener_changed()` and `notify_listener_changed_to(node)` for Splitscreen/VR or manual listener switching. `notify_listener_changed_to` extracts position/direction from a `Node3D` and updates the audio listener.

### Changed

- **Replaced use_radeon_rays with scene_type** - Boolean removed in favor of explicit enum. Backwards compatible: configs with `use_radeon_rays` (no `scene_type`) still work (true→Radeon Rays, false→Embree).
- **Editor menu naming** - "Export Static Scene" → "Export Active Scene"; "Export Dynamic Meshes" → "Export Dynamic Objects In Active Scene"; "Bake All Probe Volumes" → "Bake All Probe Volumes In Active Scene".  
Exports static `ResonanceGeometry` to OBJ+MTL via `iplSceneSaveOBJ` for visualization in external tools. `ResonanceServer.export_static_scene_to_obj(scene_root, file_base_name)` available at runtime and in editor.  
New: "Export Dynamic Objects In All Scenes In Build".

## [0.8.5] - 2026-03-02

**Pro-Source Controls and User-Defined Inputs**

### Added

- **Pro-source reflections type** - `ResonancePlayerConfig.reflections_type`: Use Global, Realtime, Baked Reverb, Baked Static Source, Baked Static Listener. Per-source choice between realtime raytracing and baked data.
- **Current Baked Source/Listener** - `current_baked_source` and `current_baked_listener` NodePaths for runtime-switchable baked references (e.g. teleport to different baked listener zones).
- **User-defined occlusion** - `occlusion_input` (Simulation / User Defined), `occlusion_value` (0-1). Script-controlled occlusion for custom models.
- **User-defined transmission** - `transmission_input`, `transmission_low`, `transmission_mid`, `transmission_high` for script-controlled transmission.
- **User-defined directivity** - `directivity_input`, `directivity_value` (0-1) for script-controlled directivity.
- **Pro-source HRTF** - `apply_hrtf_to_reflections` and `apply_hrtf_to_pathing` (Use Global / Disabled / Enabled) per source.
- **Pro-source reflections/pathing toggles** - `reflections_enabled` and `pathing_enabled_override` to enable/disable reflections or pathing per source.

### Changed

- Reflection simulation and baked data variation are now per-source; server `update_source` supports `baked_data_variation` -1 (Realtime), 0 (Reverb), 1 (Static Source), 2 (Static Listener).
- Per-source `reflections_enabled_override` and `pathing_enabled_override` passed to server; sources can exclude reflections or pathing independently.

## [0.8.2] - 2025-02-28

**Audio Bus Flexibility**

### Added

- **Configurable reverb bus** - `ResonanceRuntimeConfig.reverb_bus_name` for flexible routing. Reverb output is sent to the same bus as Direct+Pathing (`bus`).
- **Per-source reverb bus override** - `ResonancePlayerConfig.reverb_bus_override` (Use Global / Custom) and `reverb_bus_name` for selecting the reverb bus per source. Use Global = RuntimeConfig; Custom = pick from existing buses.
- **Project Settings** - `audio/nexus_resonance/reverb_bus_name` for editor/default bus setup.

### Changed

- **Removed reverb_bus_send** - Reverb output now always goes to `bus` (same as Direct+Pathing). Simpler, consistent with player_config which has no separate send.
- Reverb bus name is no longer hardcoded; default remains "ResonanceReverb" and sends to `bus`.
- Direct and pathing sound routing via `ResonancePlayer.bus` explicitly documented (was already supported).

## [0.8.1] - 2025-02-27

**Feature, Performance and Stability Update.**

### Added

- **Sample rate override** - `ResonanceRuntimeConfig.sample_rate_override` enum: Use Godot Mix Rate (default), 22050 Hz, 44100 Hz, 48000 Hz, 96000 Hz, 192000 Hz. Mismatch with Godot mix rate may affect audio (no resampling).
- **Audio frame size 2048** - New option for lower CPU usage at higher latency (Ray Tracer Settings).
- **DEVELOPERS.md** - Project overview, architecture, build, test, and release workflow for developers.
- **Unit tests** - ResonanceBakeConfig, ResonanceSceneUtils, ResonancePlayerConfig, sample_rate_override.

### Fixed

- **IPL error handling** - All `ipl*Create` calls now check `IPL_STATUS_SUCCESS`, log via ResonanceLog, and perform proper cleanup on failure (ResonanceGeometry, ResonanceMixerProcessor, ResonanceReflectionProcessor).
- **SOFA asset null checks** - HRTF init validates `hrtf_sofa_asset` and `get_data_ptr()`/`get_size()` before use; falls back to default HRTF on invalid data.
- **ResonancePlayer reverb buffer** - Stack buffer size increased to support frame size 2048 (was fixed at 512).

### Changed

- **ResonanceLogger integration** - C++ `ResonanceLog::error` and `ResonanceLog::warn` now forward to ResonanceLogger when available.
- **GDScript typing** - Added type hints to plugin callbacks, ResonanceSceneUtils, ResonanceRuntime.
- **ResonanceServerConfig** - Accepts frame size 2048; sample rate from config (supports override).

## [0.8.0] - 2025-02-24

**First public release.** Nexus Resonance brings Steam Audio (Phonon) integration to Godot 4 with physics-based spatial audio.

### Added

**Nodes & Resources**

- ResonanceProbeVolume - baked reverb and pathing data; probe visualization
- ResonanceGeometry / ResonanceDynamicGeometry - acoustic geometry for static and moving meshes
- ResonanceStaticScene - container for exported static scene assets
- ResonancePlayer - 3D audio source with occlusion, reverb, pathing (replaces AudioStreamPlayer3D)
- ResonanceListener - listener position for debug visualization
- ResonanceRuntime + ResonanceRuntimeConfig - listener updates, quality settings
- ResonanceBakeConfig - per-volume bake settings (reflections, pathing, quality)
- ResonancePlayerConfig - per-source settings (distance, occlusion, directivity, pathing, perspective)
- ResonanceMaterial - absorption, scattering, transmission; 16 presets (concrete, wood, metal, glass, brick, drywall, plaster, carpet, fabric, rubber, plastic, ceramic, marble tile, acoustic ceiling tile, gravel/sand, water)
- ResonanceSOFAAsset - custom HRTF from SOFA files for binaural playback

**Audio Features**

- HRTF spatialization with optional SOFA support
- Occlusion - raycast or volumetric model
- Reflections - baked (Convolution/Parametric/Hybrid) and realtime (0-8192 rays); optional Radeon Rays GPU acceleration
- Pathing - multi-path sound propagation (baked)
- Transmission - frequency-dependent/independent sound through walls
- Directivity - source directivity patterns
- Air absorption - distance-based attenuation
- Perspective correction - third-person spatialization adjustment
- ResonanceAudioEffect - reverb bus effect (auto-configured)

**Editor**

- Toolbar (Tools → Nexus Resonance): Export Static Scene, Export Dynamic Meshes, Bake All Probe Volumes, Clear Probe Batches, Unlink Probe Volume References
- Probe volume inspector: bake buttons, prerequisites checklist, preview settings
- Probe sphere gizmo for volume visualization
- SOFA importer for HRTF files
- Configurable bake parameters in Project Settings (`audio/nexus_resonance/bake`_*)

**Debug & Tooling**

- Debug overlay - runtime status, audio instrumentation, log
- Debug visualization - occlusion rays, reflection rays, path rays
- ResonanceLogger autoload for categorized logging

**Release & CI**

- Release workflow: addon ZIP, source ZIP, GitHub Releases with binaries
- Multi-platform CI (Linux, Windows, macOS), GUT and C++ tests, CodeQL

### Fixed

- Bake Pathing: skip geometry refresh before pathing bake to avoid Godot crash (IPL scene state conflict)
- Reflections bake: reload volumes using probe data (fixes multi-volume probe_data sync)

### Changed

- Bake parameters (num_rays, num_bounces, pathing) read from Project Settings
- Project structure: LICENSE and CHANGELOG at repo root; tests in `test/`


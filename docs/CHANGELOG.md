# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
- Configurable bake parameters in Project Settings (`audio/nexus_resonance/bake_*`)

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

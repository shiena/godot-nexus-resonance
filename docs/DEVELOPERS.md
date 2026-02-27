# Nexus Resonance - Agent Guide

Nexus Resonance is a Godot 4 addon that integrates Steam Audio (Phonon) for spatial audio: occlusion, reflections, reverb, pathing, and HRTF-based binaural rendering.

## Project Structure

```
nexus-resonance/
├── src/                    # C++ GDExtension (Steam Audio integration)
│   ├── resonance_*.cpp/h   # Core classes (Server, Player, Geometry, Baker, etc.)
│   ├── resonance_log.*     # C++ logging (ResonanceLog, forwards to ResonanceLogger)
│   ├── lib/
│   │   ├── godot-cpp/      # Godot C++ bindings
│   │   └── steamaudio/     # Steam Audio Phonon SDK
│   └── register_types.cpp  # Module init
├── audio_resonance_tool/   # Godot test project
│   └── addons/nexus_resonance/
│       ├── plugin.gd       # EditorPlugin
│       ├── scripts/        # GDScript (ResonanceRuntime, Config, etc.)
│       ├── editor/         # Bake runner, inspectors, gizmos
│       ├── bin/            # Built .dll/.so (GDExtension)
│       └── doc_classes/    # API docs (XML)
├── core/                   # Valve Steam Audio C++ core (tests, benchmarks)
└── SConstruct              # SCons build
```

## Architecture

- **ResonanceServer** (C++): Singleton, owns Steam Audio context, simulator, scene, HRTF.
- **ResonanceRuntime** (GDScript): Scene node, drives init, listener fallback, reverb bus.
- **ResonanceRuntimeConfig** (GDScript): Resource with all runtime settings (sample rate, frame size, reflection type, etc.).
- **ResonancePlayer** / **ResonanceAmbisonicPlayer** (C++): Audio sources with direct, reverb, pathing.
- **ResonanceGeometry** (C++): Mesh → Steam Audio scene (static/dynamic, asset or runtime).
- **ResonanceProbeVolume** (C++): Baked reverb/pathing probes.

## Build

```bash
# Install Steam Audio SDK first
python3 scripts/install_steam_audio.py

# Build (from project root)
scons

# Windows cross-compile from Linux
scons platform=windows
```

Output: `audio_resonance_tool/addons/nexus_resonance/bin/nexus_resonance.dll` (or .so/.dylib).

## Test

Unit tests (GUT) in `audio_resonance_tool/test/unit/`:

- `test_resonance_config.gd` - ResonanceRuntimeConfig
- `test_probe_data_loader.gd` - Probe data loading
- `test_probe_data_saver.gd` - Probe data saving
- `test_resonance_bake_settings.gd` - Bake settings

Run via Godot with GUT addon or CLI.

## Release Workflow

1. Update version in `src/resonance_constants.h` (NEXUS_RESONANCE_VERSION).
2. Tag: `git tag v0.8.1`
3. Push tag: triggers `.github/workflows/release.yml` (builds Windows/Linux, creates GitHub Release).

## Coding Conventions

- **GDScript**: See `.cursor/rules/gdscript-nexus.mdc` - `@export_group`, `@export_enum`, Setter mit `_warn_restart()`.
- **C++**: See `.cursor/rules/cpp-gdextension.mdc` - GDExtension patterns, `CLASS_BINDING`.
- **Errors**: Use `ResonanceLog::error()` / `ResonanceLog::warn()` in C++; these forward to ResonanceLogger when available.
- **IPL calls**: Always check `IPL_STATUS_SUCCESS`; on failure log and cleanup.

## Key Files for Common Tasks

| Task | Files |
|------|-------|
| Add runtime config option | `resonance_runtime_config.gd`, `resonance_server_config.cpp/h` |
| Add Steam Audio feature | `resonance_server.cpp`, `resonance_player.cpp`, processors |
| Editor UI | `plugin.gd`, `editor/resonance_*.gd` |
| Bake pipeline | `resonance_baker.cpp`, `editor/resonance_bake_runner.gd` |

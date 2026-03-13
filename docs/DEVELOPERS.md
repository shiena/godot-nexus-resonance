# Nexus Resonance - Agent Guide

Nexus Resonance is a Godot 4 addon that integrates Steam Audio (Phonon) for spatial audio: occlusion, reflections, reverb, pathing, and HRTF-based binaural rendering.

## Project Structure

```
nexus-resonance/
├── src/                    # C++ GDExtension (Steam Audio integration)
│   ├── resonance_*.cpp/h   # Core classes (Server, Player, Geometry, Baker, etc.)
│   ├── resonance_log.*     # C++ logging (ResonanceLog, forwards to ResonanceLogger)
│   ├── test/               # C++ unit tests (Catch2)
│   ├── lib/
│   │   ├── godot-cpp/      # Godot C++ bindings (submodule)
│   │   ├── catch2/         # Catch2 test framework (submodule, v2.x)
│   │   ├── pffft/          # FFT library for iOS (submodule)
│   │   ├── libmysofa/      # HRTF/SOFA reader for iOS (submodule)
│   │   └── steamaudio/     # Steam Audio Phonon SDK (downloaded via install script)
│   └── register_types.cpp  # Module init
├── audio_resonance_tool/   # Godot test project
│   └── addons/nexus_resonance/
│       ├── plugin.gd       # EditorPlugin
│       ├── scripts/        # GDScript (ResonanceRuntime, Config, etc.)
│       ├── editor/         # Bake runner, inspectors, gizmos
│       ├── bin/            # Built .dll/.so/.dylib/.a (GDExtension)
│       └── doc_classes/    # API docs (XML)
├── Makefile                # Cross-platform build targets
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
# Fetch submodules
git submodule update --init --recursive

# Install Steam Audio SDK
python3 scripts/install_steam_audio.py

# Build for current platform
scons

# Platform-specific builds via Makefile
make build-windows    # Windows x64 (cross-compile with mingw)
make build-linux      # Linux x64
make build-macos      # macOS (universal)
make build-android    # Android arm64 + x86_64
make build-ios        # iOS arm64 (macOS only; builds pffft/libmysofa deps)
```

Output: `audio_resonance_tool/addons/nexus_resonance/bin/`

## Test

Unit tests (GUT) in `audio_resonance_tool/test/unit/`:

- `test_resonance_config.gd` - ResonanceRuntimeConfig
- `test_probe_data_loader.gd` - Probe data loading
- `test_probe_data_saver.gd` - Probe data saving
- `test_resonance_bake_settings.gd` - Bake settings

C++ unit tests (Catch2) in `src/test/`:

```bash
scons build_tests=1
./build/tests/nexus_resonance_tests
```

## Release Workflow

1. Update version in `src/resonance_constants.h` (NEXUS_RESONANCE_VERSION).
2. Tag: `git tag v0.8.1`
3. Push tag: triggers `.github/workflows/release.yml` which builds all platforms (Linux, Windows, macOS, Android, iOS) and creates a GitHub Release with a unified addon zip.

## CI/CD

- **build.yml** - Builds all platforms on push/PR to main (Linux/Windows/Android on ubuntu, macOS/iOS on macos-latest)
- **release.yml** - Full build + GitHub Release on version tags (`v*`)
- **tests.yml** - C++ unit tests + GDScript GUT tests on push/PR
- **codeql.yml** - CodeQL security analysis (manual trigger)

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

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

Run via Godot with GUT addon or CLI.

**Manual regression (geometry teardown):** No C++ unit test covers full Phonon teardown. After changes to `ResonanceGeometry` cleanup, verify in a running project: attach **ResonanceDynamicGeometry** under a `MeshInstance3D`, run the game, then **change scene** (`change_scene_to_file` / equivalent). It must not crash; dynamic meshes are removed from the object’s `sub_scene` before `iplSceneRelease`, not from the global scene.

## Release Workflow

1. Update version in `src/resonance_constants.h` (NEXUS_RESONANCE_VERSION).
2. Tag: `git tag v0.8.1`
3. Push tag: triggers `.github/workflows/release.yml` which builds all platforms (Linux, Windows, macOS, Android, iOS) and creates a GitHub Release with a unified addon zip.

## Pre Push / PR (Checklist)


| Was                           | Wie                                                                                                                                                                                                         |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **C++-Format (wie Linux-CI)** | `[scripts/check_clang_format.sh](scripts/check_clang_format.sh)` — bei Fehlern `[scripts/format_cpp.sh](scripts/format_cpp.sh)`, dann erneut prüfen.                                                        |
| **Alles in einem (lokal)**    | `[scripts/prep_push.sh](scripts/prep_push.sh)` (Bash / Git Bash) oder `[scripts/prep_push.ps1](scripts/prep_push.ps1)` (PowerShell; `CLANG_FORMAT_BIN` oder `.tools\LLVM-14-extract\bin\clang-format.exe`). |
| **C++-Unit-Tests**            | `scons` (baut Tests standardmäßig), dann `build/tests/nexus_resonance_tests` bzw. `.exe`.                                                                                                                   |
| **GDScript**                  | GUT läuft in **tests.yml** auf geänderten Pfaden; bei relevanten `.gd`-Änderungen lokal GUT ausführen oder auf CI vertrauen.                                                                                |
| **Version sichtbar**          | Bei Release: `CHANGELOG` + `NEXUS_RESONANCE_VERSION` in `src/resonance_constants.h`.                                                                                                                        |


Cursor: Regel `[.cursor/rules/before-push.mdc](.cursor/rules/before-push.mdc)` — Assistent soll bei „will pushen“ / Release-Vorhaben Format-Check und C++-Tests ausführen.

## CI/CD

- **build.yml** - Builds all platforms on push/PR to main (Linux/Windows/Android on ubuntu, macOS/iOS on macos-latest)
- **release.yml** - Full build + GitHub Release on version tags (`v`*)
- **tests.yml** - C++unit tests + GDScript GUT tests on push/PR; **clang-format-14** check via `[scripts/check_clang_format.sh](scripts/check_clang_format.sh)` (`--dry-run -Werror` — no in-tree edits, no `git diff` drift). Those `**.sh` files must be committed** with the workflow; if they are missing on GitHub, the format step fails with **exit 127** (script/binary not found). Before pushing C++ changes, run `[scripts/format_cpp.sh](scripts/format_cpp.sh)` (or `CLANG_FORMAT_BIN=…` if your binary is not named `clang-format-14`). **clang-tidy** (first 80 `src` units, `bugprone-`* / `cert-*` / `clang-analyzer-*`) runs with warnings as errors but **excludes** `bugprone-easily-swappable-parameters`, which is too noisy for multi-`float` / multi-`int` engine callbacks.
- **codeql.yml** - CodeQL security analysis (manual trigger)

## Known Limits and Workarounds

- **Editor shutdown**: Do not call `ResourceSaver.remove_resource_format_saver` / `ResourceLoader.remove_resource_format_loader` in plugin `_exit_tree`; Godot may tear these down before the plugin, causing SIGSEGV.
- **GDExtension unload**: `clear_probe_batches` is now called in `_disable_plugin` only when `Engine.has_singleton("ResonanceServer")` is true. On editor close the GDExtension may be unloaded first—then the singleton is gone and we skip safely. When the user disables the plugin from project settings, we clean up.
- **Probe volume deletion**: Probe Volume clears refs on EXIT_TREE; ResonancePlayer auto-clears `pathing_probe_volume` when the target node is gone. If the error still occurs, use Tools > Unlink Probe Volume References before deleting.

## Coding Conventions

- **GDScript**: See `.cursor/rules/gdscript-nexus.mdc` - `@export_group`, `@export_enum`, Setter mit `_warn_restart()`.
- **C++**: See `.cursor/rules/cpp-gdextension.mdc` - GDExtension patterns, `CLASS_BINDING`.
- **Errors**: Use `ResonanceLog::error()` / `ResonanceLog::warn()` in C++; these forward to ResonanceLogger when available.
- **IPL calls**: Always check `IPL_STATUS_SUCCESS`; on failure log and cleanup.

## Audio Processor Pattern (Steam Audio Unity Alignment)

All Steam Audio processors (Direct, Reflection, Path, Mixer, Ambisonic) follow a consistent pattern:

**Initialization order (Create)**

1. Context and config (sample rate, frame size, ambisonic order).
2. IPL effect objects (e.g. `iplDirectEffectCreate`, `iplBinauralEffectCreate`).
3. Buffers (`iplAudioBufferAllocate`).
4. Set `InitFlags` per successful step.

**Release order (Reverse of create)**

1. Release effect objects.
2. Free buffers (requires context).
3. Clear context reference.
4. Reset `InitFlags` to `NONE`.

**InitFlags**

- Bitwise enum per processor (e.g. `DirectInitFlags`, `AmbisonicInitFlags`).
- `process()` only runs when all required flags are set; avoids partial-init crashes.
- On failure, processors support passthrough fallback (Direct, Ambisonic) instead of silence.

**Process guards**

- Null checks at process entry: context, input/output buffers.
- InitFlags guard before processing.
- Processors may return early with passthrough or silence on invalid state.

**Double-buffering (audio thread)**

- Listener coordinates, parametric reverb cache, HRTF, ReflectionMixer use main-write / audio-read double-buffers.
- Atomic flags trigger swap on consume; lock-free for the audio hot path.

## Reference: Steam Audio Unity Bug to Avoid

In `references/steam-audio-4.8.1/unity/src/native/mix_return_effect.cpp` (lines 150–164), the `memset` for `inBuffer` and `outBuffer` incorrectly uses `effect->reflectionsBuffer.data[i]` instead of `effect->inBuffer.data[i]` and `effect->outBuffer.data[i]`. When adding similar lazy-init buffer allocations, ensure the correct buffer is used in each loop.

## Key Files for Common Tasks


| Task                      | Files                                                                             |
| ------------------------- | --------------------------------------------------------------------------------- |
| Add runtime config option | `resonance_runtime_config.gd`, `resonance_server_config.cpp/h`                    |
| Add Steam Audio feature   | `resonance_server*.cpp`, `resonance_server.h`, `resonance_player.cpp`, processors |
| Editor UI                 | `plugin.gd`, `editor/resonance_*.gd`                                              |
| Bake pipeline             | `resonance_baker.cpp`, `editor/resonance_bake_runner.gd`                          |



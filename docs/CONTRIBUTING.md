# Contributing to Nexus Resonance

Thank you for your interest in contributing. This document provides guidelines for contributing to the project.

## Project Structure

- `audio_resonance_tool/` – Godot 4 project containing the Nexus Resonance addon
- `audio_resonance_tool/addons/nexus_resonance/` – Nexus Resonance plugin (GDScript + GDExtension)
- `src/` – C++ GDExtension source (ResonanceServer, Steam Audio integration)

## Development Setup

1. Clone the repository (with submodules):
   ```powershell
   git clone --recurse-submodules <repo-url>
   ```
   Or, if already cloned:
   ```powershell
   git submodule update --init --recursive
   ```
   **First-time setup** (when migrating from bundled libs):  
   Run `.\scripts\init_submodules.ps1` to add submodules, then `.\scripts\setup_libs.ps1`.
2. Install Steam Audio SDK (downloads from [ValveSoftware/steam-audio](https://github.com/ValveSoftware/steam-audio) releases):
   ```powershell
   python scripts/install_steam_audio.py
   ```
   Or run the combined setup:
   ```powershell
   .\scripts\setup_libs.ps1
   ```
3. Build the GDExtension: `scons`
4. Open `audio_resonance_tool/` in Godot 4.6.
5. Enable the Nexus Resonance plugin in Project Settings → Plugins.

### Library References (Git Submodules)

Libraries are referenced from GitHub, not bundled:

- **[godot-cpp](https://github.com/godotengine/godot-cpp)** – Godot C++ bindings (branch 4.5)
- **[Catch2](https://github.com/catchorg/Catch2)** – C++ unit tests
- **Steam Audio** – Fetched via `install_steam_audio.py` from [ValveSoftware/steam-audio](https://github.com/ValveSoftware/steam-audio) releases

## Running Tests

### GDScript (GUT)

Unit tests use the [GUT](https://github.com/bitwes/Gut) framework.

**In the editor:** Project → Tools → GUT → Run All (with `res://test/unit` as directory).

**Command line (PowerShell):**
```powershell
cd audio_resonance_tool
.\run_tests.ps1
```

Set `$env:GODOT_PATH` to your Godot executable if it is not in PATH.

### C++ Unit Tests

C++ tests use [Catch2](https://github.com/catchorg/Catch2). Build with tests enabled and run:

```powershell
scons build_tests=1
.\build\tests\nexus_resonance_tests.exe
```

On Linux/macOS: `./build/tests/nexus_resonance_tests`

## Code Style

- **GDScript:** English only in scripts. Use `##` for GDDoc comments on public methods and properties.
- **Naming:** snake_case for variables/functions, PascalCase for classes.
- **Formatting:** Match existing style; use Godot's built-in formatter where applicable.

## Pull Request Process

1. Create a branch from `main` or `master`.
2. Make your changes. Ensure tests pass.
3. Update `CHANGELOG.md` under `[Unreleased]` if the change is user-facing.
4. Submit a pull request with a clear description.

## Reporting Bugs

Use the [Bug Report](.github/ISSUE_TEMPLATE/bug_report.yml) template. Include Godot version, OS, and relevant output logs.

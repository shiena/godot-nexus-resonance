# Nexus Resonance Unit Tests

## GDScript (GUT)

Tests use the [GUT](https://github.com/bitwes/Gut) framework. Unit tests live in `test/unit/`.

## Running Tests

### GUT Panel (Editor)

1. Open the project in Godot.
2. Open the GUT panel (bottom or via **Project > Tools > GUT**).
3. Set **Directories** to `res://test/unit` (or use `.gutconfig.json` defaults).
4. Click **Run All** to run all tests.

### Command Line (CI)

GUT requires the project's global script class cache to be populated before running tests. Run import once (or after adding GUT):

```powershell
& "C:\Godot\Godot_v4.6-stable_win64\Godot_v4.6-stable_win64.exe" --headless --import
```

Then run the tests:

```powershell
& "C:\Godot\Godot_v4.6-stable_win64\Godot_v4.6-stable_win64.exe" -s addons/gut/gut_cmdln.gd -gdir=res://test/unit -gexit
```

Or use the helper script (set `$env:GODOT_PATH` if godot is not in PATH):

```powershell
.\run_tests.ps1
```

- `--headless --import` – populate `global_script_class_cache.cfg` (required before first CLI run)
- `-gdir=res://test/unit` – directory containing test scripts
- `-gexit` – exit with code 0 when all pass, non‑zero on failure (for CI)

### Test Files

| Script | Covers |
|--------|--------|
| `test/unit/test_resonance_config.gd` | ResonanceConfig (get_fallback_config, get_resonance_config, type coercion, get_effective_realtime_rays) |
| `test/unit/test_probe_data_loader.gd` | ResonanceProbeDataLoader (_parse_tres_data, extensions, handles_type, recognize_path, get_resource_type) |
| `test/unit/test_probe_data_saver.gd` | ResonanceProbeDataSaver (_recognize, _get_recognized_extensions, _save) |
| `test/unit/test_resonance_bake_settings.gd` | ResonanceBakeSettings (DEFAULTS, HINTS, register_bake_project_settings) |
| `test/unit/test_probe_toolbar.gd` | ProbeToolbar (_is_valid_config, _get_safe_resonance_config, _collect_resonance_geometry) |

## C++ Unit Tests

C++ tests live in `src/test/` and use [Catch2](https://github.com/catchorg/Catch2). From the repository root:

```bash
scons build_tests=1
./build/tests/nexus_resonance_tests   # Linux/macOS
.\build\tests\nexus_resonance_tests.exe  # Windows
```

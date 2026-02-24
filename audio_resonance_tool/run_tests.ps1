# Run Nexus Resonance unit tests via GUT.
# Requires godot in PATH, or set $GodotPath below.

$GodotPath = "godot"
if ($env:GODOT_PATH) { $GodotPath = $env:GODOT_PATH }

$ProjectRoot = $PSScriptRoot
Push-Location $ProjectRoot

# Ensure global script class cache is populated (required for GUT CLI)
Write-Host "Importing project (populating class cache)..."
& $GodotPath --headless --import
if ($LASTEXITCODE -ne 0) { Pop-Location; exit $LASTEXITCODE }

Write-Host "Running GUT unit tests..."
& $GodotPath -s addons/gut/gut_cmdln.gd -gdir=res://test/unit -gexit
$exit = $LASTEXITCODE
Pop-Location
exit $exit

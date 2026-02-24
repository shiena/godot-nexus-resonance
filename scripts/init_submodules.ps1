# Nexus Resonance - Initialize git submodules (first-time setup)
# Run once when migrating to submodules or setting up a new clone.
# After this, use setup_libs.ps1 for regular setup.

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

Write-Host "Nexus Resonance - Initialize Submodules" -ForegroundColor Cyan
Write-Host ""

$libDir = "src\lib"
if (-not (Test-Path $libDir)) {
    New-Item -ItemType Directory -Path $libDir -Force | Out-Null
}

# Add godot-cpp submodule if not present
$godotCpp = Join-Path $libDir "godot-cpp"
if (-not (Test-Path (Join-Path $godotCpp ".git"))) {
    if (Test-Path $godotCpp) {
        Write-Host "Removing existing godot-cpp (will be replaced by submodule)..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $godotCpp
    }
    Write-Host "Adding godot-cpp submodule..."
    git submodule add -b 4.5 https://github.com/godotengine/godot-cpp $godotCpp
    if ($LASTEXITCODE -ne 0) { exit 1 }
} else {
    Write-Host "godot-cpp submodule already present." -ForegroundColor Gray
}

# Add catch2 submodule if not present
$catch2 = Join-Path $libDir "catch2"
if (-not (Test-Path (Join-Path $catch2 ".git"))) {
    if (Test-Path $catch2) {
        Write-Host "Removing existing catch2 (will be replaced by submodule)..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $catch2
    }
    Write-Host "Adding catch2 submodule..."
    git submodule add -b devel https://github.com/catchorg/Catch2 $catch2
    if ($LASTEXITCODE -ne 0) { exit 1 }
} else {
    Write-Host "catch2 submodule already present." -ForegroundColor Gray
}

Write-Host ""
Write-Host "Submodules initialized. Run .\scripts\setup_libs.ps1 to fetch Steam Audio." -ForegroundColor Green

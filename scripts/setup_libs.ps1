# Nexus Resonance - Library setup script
# Fetches godot-cpp and catch2 via git submodules (or direct clone fallback), Steam Audio via install script.
# Run from project root after clone: .\scripts\setup_libs.ps1

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $Root ".gitmodules"))) {
    Write-Error ".gitmodules not found. Run this from the project root or ensure .gitmodules exists."
}

Set-Location $Root

Write-Host "Nexus Resonance - Library Setup" -ForegroundColor Cyan
Write-Host ""

# 1. Git submodules (godot-cpp, catch2)
$libDir = "src\lib"
$godotCpp = Join-Path $libDir "godot-cpp"
$catch2 = Join-Path $libDir "catch2"

function Test-SubmodulePopulated { param($Path) Test-Path (Join-Path $Path ".git") }

# Try git submodule first (works when submodules are committed in the repo)
Write-Host "Initializing git submodules..." -ForegroundColor Yellow
git submodule update --init --recursive 2>&1 | Out-Null
$gitOk = ($LASTEXITCODE -eq 0) -and (Test-SubmodulePopulated $godotCpp) -and (Test-SubmodulePopulated $catch2)

if (-not $gitOk) {
    # Fallback: clone directly when submodules are not in repo (e.g. after transfer from non-git source)
    Write-Host "Submodules not in repo index. Cloning directly..." -ForegroundColor Yellow
    if (-not (Test-Path $libDir)) { New-Item -ItemType Directory -Path $libDir -Force | Out-Null }
    if (-not (Test-SubmodulePopulated $godotCpp)) {
        if (Test-Path $godotCpp) { Remove-Item -Recurse -Force $godotCpp }
        git clone -b 4.5 --depth 1 https://github.com/godotengine/godot-cpp $godotCpp
        if ($LASTEXITCODE -ne 0) { Write-Error "Failed to clone godot-cpp." }
    }
    if (-not (Test-SubmodulePopulated $catch2)) {
        if (Test-Path $catch2) { Remove-Item -Recurse -Force $catch2 }
        git clone -b devel --depth 1 https://github.com/catchorg/Catch2 $catch2
        if ($LASTEXITCODE -ne 0) { Write-Error "Failed to clone Catch2." }
    }
}

# 2. Steam Audio (download from Valve releases)
Write-Host ""
Write-Host "Installing Steam Audio SDK..." -ForegroundColor Yellow
python scripts/install_steam_audio.py
if ($LASTEXITCODE -ne 0) {
    Write-Error "Steam Audio install failed."
}

Write-Host ""
Write-Host "Library setup complete." -ForegroundColor Green
Write-Host "You can now run: scons"

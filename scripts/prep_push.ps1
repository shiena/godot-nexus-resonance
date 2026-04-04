# Local pre-push checks (Windows): clang-format-14 dry-run, SCons, C++ unit tests.
# Optional: $env:CLANG_FORMAT_BIN = path to clang-format.exe (e.g. LLVM 14).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$cf = $env:CLANG_FORMAT_BIN
if (-not $cf) {
    $candidate = Join-Path $root '.tools\LLVM-14-extract\bin\clang-format.exe'
    if (Test-Path $candidate) { $cf = $candidate }
}
if (-not $cf -or -not (Test-Path $cf)) {
    Write-Error "clang-format not found. Install LLVM 14, set CLANG_FORMAT_BIN, or extract to .tools\LLVM-14-extract (see DEVELOPERS.md)."
}

$files = Get-ChildItem -Path (Join-Path $root 'src') -Recurse -Include *.cpp, *.h -File |
    Where-Object { $_.FullName -notmatch '\\lib\\' -and $_.FullName -notmatch '\\gen\\' }
$all = @($files | ForEach-Object { $_.FullName })
if ($all.Count -eq 0) { Write-Error 'No C++ files to check.' }
& $cf --dry-run -Werror @all
if ($LASTEXITCODE -ne 0) {
    Write-Host "clang-format failed (exit $LASTEXITCODE). Fix: clang-format -i on src/*.cpp,*.h excluding lib/gen, or bash scripts/format_cpp.sh" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "clang-format: OK ($($all.Count) files)"

scons -j8

$testExe = Join-Path $root 'build\tests\nexus_resonance_tests.exe'
if (-not (Test-Path $testExe)) { Write-Error "Missing $testExe; run scons from repo root." }
& $testExe
Write-Host 'prep_push.ps1: OK'

#!/usr/bin/env bash
# Local parity with Linux CI before git push: clang-format-14 check, SCons build, C++ unit tests.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

bash scripts/check_clang_format.sh

NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
scons -j"$NPROC"

TEST="./build/tests/nexus_resonance_tests"
[[ -f "$TEST" ]] || TEST="./build/tests/nexus_resonance_tests.exe"
if [[ ! -f "$TEST" ]]; then
	echo "ERROR: C++ test binary not found under build/tests/" >&2
	exit 1
fi
"$TEST"

echo "prep_push.sh: OK (format + build + nexus_resonance_tests)"

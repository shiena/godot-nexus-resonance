#!/usr/bin/env bash
# Apply .clang-format to all project C++ under src/ (excludes lib/, gen/).
# Requires: clang-format-14 on PATH (same as CI).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

FMT="${CLANG_FORMAT_BIN:-clang-format-14}"
if ! command -v "$FMT" >/dev/null 2>&1; then
	echo "ERROR: '$FMT' not found. Install clang-format-14 or set CLANG_FORMAT_BIN." >&2
	exit 1
fi

find src -type f \( -name "*.cpp" -o -name "*.h" \) ! -path "*/lib/*" ! -path "*/gen/*" -print0 |
	xargs -0 "$FMT" -i

echo "Formatted C++ sources with $FMT"

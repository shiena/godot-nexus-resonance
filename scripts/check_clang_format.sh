#!/usr/bin/env bash
# Match Linux CI: verify C++ under src/ matches .clang-format (no edits).
# Requires: clang-format-14 on PATH (e.g. apt install clang-format-14).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

FMT="${CLANG_FORMAT_BIN:-clang-format-14}"
if ! command -v "$FMT" >/dev/null 2>&1; then
	echo "ERROR: '$FMT' not found. Install LLVM 14 clang-format, e.g.:" >&2
	echo "  Ubuntu/Debian: sudo apt-get install -y clang-format-14" >&2
	echo "  Override: CLANG_FORMAT_BIN=/path/to/clang-format $0" >&2
	exit 1
fi

echo "Using:"
"$FMT" --version

# shellcheck disable=SC2046
mapfile -t FILES < <(
	find src -type f \( -name "*.cpp" -o -name "*.h" \) ! -path "*/lib/*" ! -path "*/gen/*" | sort
)

if [[ ${#FILES[@]} -eq 0 ]]; then
	echo "No files to check."
	exit 0
fi

# --dry-run -Werror: exit non-zero if any file would change (no working-tree mutation).
printf '%s\0' "${FILES[@]}" | xargs -0 "$FMT" --dry-run -Werror

echo "clang-format: OK (${#FILES[@]} files)"

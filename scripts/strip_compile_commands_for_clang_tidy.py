#!/usr/bin/env python3
"""Remove GCC-only flags from compile_commands.json so clang-tidy accepts the database."""

from __future__ import annotations

import argparse
import json
import re
import sys

# g++ may emit these; Clang's driver errors with "unknown argument" when clang-tidy replays the line.
_STRIP_EXACT = frozenset({"-fno-gnu-unique"})


def _clean_command_string(cmd: str) -> str:
    out = cmd
    for flag in _STRIP_EXACT:
        out = re.sub(rf"(^|\s){re.escape(flag)}(\s|$)", " ", out)
    return re.sub(r"  +", " ", out).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        default="compile_commands.json",
        help="Path to compile_commands.json (default: ./compile_commands.json)",
    )
    args = parser.parse_args()
    path = args.path
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    for entry in data:
        if "arguments" in entry:
            entry["arguments"] = [a for a in entry["arguments"] if a not in _STRIP_EXACT]
        elif "command" in entry:
            entry["command"] = _clean_command_string(entry["command"])
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

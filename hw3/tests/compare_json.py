#!/usr/bin/env python3
"""Compare two JSON files semantically (ignoring whitespace and object-key order)."""
from __future__ import annotations

import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} EXPECTED.json ACTUAL.json", file=sys.stderr)
        return 2

    expected_path, actual_path = map(Path, sys.argv[1:])
    try:
        expected = json.loads(expected_path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"cannot read expected JSON {expected_path}: {exc}", file=sys.stderr)
        return 2
    try:
        actual = json.loads(actual_path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"actual output is not valid JSON ({actual_path}): {exc}", file=sys.stderr)
        return 1

    if expected == actual:
        return 0

    expected_pretty = json.dumps(expected, ensure_ascii=False, indent=2, sort_keys=True).splitlines()
    actual_pretty = json.dumps(actual, ensure_ascii=False, indent=2, sort_keys=True).splitlines()
    import difflib

    print(f"JSON differs from golden file: {expected_path}", file=sys.stderr)
    diff = difflib.unified_diff(
        expected_pretty,
        actual_pretty,
        fromfile=str(expected_path),
        tofile=str(actual_path),
        lineterm="",
    )
    print("\n".join(diff), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

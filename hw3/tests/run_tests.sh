#!/usr/bin/env bash
set -uo pipefail
BIN="${1:-./funny_analyzer}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
pass=0
fail=0

for file in "$ROOT"/semantic/positive/*.funny; do
  [[ -e "$file" ]] || continue
  name="$(basename "$file")"
  expected="${file%.funny}.expected.json"
  err=$(mktemp)
  json=$(mktemp)

  if [[ ! -f "$expected" ]]; then
    echo "FAIL positive: $name (missing golden JSON: $(basename "$expected"))"
    ((fail+=1))
  elif FUNNY_TEST_MODE=1 "$BIN" "$file" >"$json" 2>"$err"; then
    if [[ -s "$err" ]]; then
      echo "FAIL positive: $name (unexpected diagnostics)"
      cat "$err"
      ((fail+=1))
    elif python3 "$ROOT/compare_json.py" "$expected" "$json" 2>/dev/null; then
      echo "PASS positive: $name (AST matches golden JSON)"
      ((pass+=1))
    else
      # Print a structural diff to explain the failure.
      python3 "$ROOT/compare_json.py" "$expected" "$json" 2>&1 || true
      echo "FAIL positive: $name (annotated AST differs from golden JSON)"
      ((fail+=1))
    fi
  else
    echo "FAIL positive: $name (non-zero exit)"
    cat "$err"
    ((fail+=1))
  fi
  rm -f "$err" "$json"
done

for file in "$ROOT"/semantic/negative/*.funny; do
  [[ -e "$file" ]] || continue
  name="$(basename "$file")"
  err=$(mktemp)
  json=$(mktemp)
  expected="${file%.funny}.expect"

  if FUNNY_TEST_MODE=1 "$BIN" "$file" >"$json" 2>"$err"; then
    echo "FAIL negative: $name (expected semantic errors)"
    ((fail+=1))
  elif grep -Eq 'semantic error at [0-9]+:[0-9]+ \(offset [0-9]+\):' "$err" && python3 -m json.tool "$json" >/dev/null 2>&1; then
    missing=0
    if [[ -f "$expected" ]]; then
      while IFS= read -r phrase || [[ -n "$phrase" ]]; do
        [[ -z "$phrase" ]] && continue
        if ! grep -Fq -- "$phrase" "$err"; then
          echo "  missing expected diagnostic: $phrase"
          missing=1
        fi
      done < "$expected"
    else
      echo "  missing expected diagnostic file: $expected"
      missing=1
    fi
    if [[ "$missing" -eq 0 ]]; then
      echo "PASS negative: $name (positioned diagnostics match expectations)"
      ((pass+=1))
    else
      echo "FAIL negative: $name (expected diagnostic missing)"
      cat "$err"
      ((fail+=1))
    fi
  else
    echo "FAIL negative: $name (missing positioned diagnostics or valid partial AST JSON)"
    cat "$err"
    ((fail+=1))
  fi
  rm -f "$err" "$json"
done

echo "TOTAL: pass=$pass fail=$fail"
[[ "$fail" -eq 0 ]]

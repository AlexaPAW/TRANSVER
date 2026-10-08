#!/usr/bin/env bash
set -u
BIN=${1:-./funny_parser}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PASS=0
FAIL=0

for src in "$ROOT"/tests/positive/*.funny; do
  exp="${src%.funny}.json"
  out=$(mktemp)
  err=$(mktemp)
  if FUNNY_TEST_MODE=1 timeout 2s "$BIN" "$src" --compact >"$out" 2>"$err" && diff -u "$exp" "$out" >/dev/null; then
    echo "PASS positive: $(basename "$src")"
    PASS=$((PASS+1))
  else
    echo "FAIL positive: $(basename "$src")"
    diff -u "$exp" "$out" || true
    cat "$err" >&2
    FAIL=$((FAIL+1))
  fi
  rm -f "$out" "$err"
done

for src in "$ROOT"/tests/negative/*.funny; do
  [[ "$src" == *.expect ]] && continue
  exp="${src%.funny}.expect"
  out=$(mktemp)
  err=$(mktemp)
  if FUNNY_TEST_MODE=1 timeout 2s "$BIN" "$src" >"$out" 2>"$err"; then
    echo "FAIL negative (accepted): $(basename "$src")"
    FAIL=$((FAIL+1))
  elif grep -F -q -f "$exp" "$err"; then
    echo "PASS negative: $(basename "$src")"
    PASS=$((PASS+1))
  else
    echo "FAIL negative (diagnostic mismatch): $(basename "$src")"
    cat "$err" >&2
    FAIL=$((FAIL+1))
  fi
  rm -f "$out" "$err"
done

echo "TOTAL: pass=$PASS fail=$FAIL"
[[ $FAIL -eq 0 ]]

#!/usr/bin/env bash
# Host tests. Need no Tab5 and no Arduino toolchain: every unit under test is
# deliberately free of Arduino and LVGL dependencies.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CXX="${CXX:-c++}"
FLAGS=(-std=c++17 -Wall -Wextra -Werror)
FW="$ROOT/firmware/beer_terminal"

# Some macOS Command Line Tools installs keep a stale usr/include/c++/v1 that
# shadows the SDK's libc++, so <cstdio> cannot be found. Detect once.
EXTRA=()
if ! "$CXX" "${FLAGS[@]}" -x c++ -fsyntax-only - <<<'#include <cstdio>
int main(){}' 2>/dev/null; then
  SDK="$(xcrun --show-sdk-path 2>/dev/null || true)"
  if [[ -d "$SDK/usr/include/c++/v1" ]]; then
    EXTRA=(-nostdinc++ -isystem "$SDK/usr/include/c++/v1")
    echo "note: falling back to the SDK libc++ at $SDK/usr/include/c++/v1" >&2
  fi
fi

run() { # name, sources...
  local name="$1"; shift
  "$CXX" "${FLAGS[@]}" "${EXTRA[@]+"${EXTRA[@]}"}" -o "$OUT/$name" "$@"
  "$OUT/$name"
  echo
}

run test_catalog_layout "$ROOT/tests/test_catalog_layout.cpp" "$FW/catalog_layout.cpp"
run test_catalog_codec  "$ROOT/tests/test_catalog_codec.cpp"  "$FW/catalog_codec.cpp"

echo "host tests passed"

#!/usr/bin/env bash
# Host test for the catalog grid geometry. Needs no Tab5 and no Arduino toolchain.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CXX="${CXX:-c++}"
SRC=("$ROOT/tests/test_catalog_layout.cpp" "$ROOT/firmware/beer_terminal/catalog_layout.cpp")
FLAGS=(-std=c++17 -Wall -Wextra -Werror)

build() { "$CXX" "${FLAGS[@]}" "$@" -o "$OUT/test_catalog_layout" "${SRC[@]}"; }

if ! build 2>"$OUT/err"; then
  # Some macOS Command Line Tools installs keep a stale usr/include/c++/v1 that
  # shadows the SDK's libc++, so <cstdio> cannot be found. Point at the SDK copy.
  SDK="$(xcrun --show-sdk-path 2>/dev/null || true)"
  if [[ -d "$SDK/usr/include/c++/v1" ]] && \
     build -nostdinc++ -isystem "$SDK/usr/include/c++/v1" 2>>"$OUT/err"; then
    echo "note: falling back to the SDK libc++ at $SDK/usr/include/c++/v1" >&2
  else
    cat "$OUT/err" >&2
    exit 1
  fi
fi

"$OUT/test_catalog_layout"

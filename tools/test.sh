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
# ArduinoJson is header-only, so the wire protocol compiles on the host too.
AJ="$ROOT/.arduino/user/libraries/ArduinoJson/src"

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
run test_api_protocol   "$ROOT/tests/test_api_protocol.cpp"   "$FW/api_protocol.cpp" -I"$AJ"

# The Apps Script backend is JavaScript, so Node checks its syntax and exercises
# its validators without deploying anything to Google.
if command -v node >/dev/null; then
  for f in "$ROOT"/backend/apps-script/*.gs; do
    cp "$f" "$OUT/$(basename "$f").js"
    node --check "$OUT/$(basename "$f").js"
  done
  node -e '
    const fs=require("fs"),p=process.argv[1];
    const m=fs.readFileSync(p,"utf8").match(/<script>([\s\S]*?)<\/script>/g)||[];
    fs.writeFileSync(process.argv[2], m.map(s=>s.replace(/<\/?script>/g,"")).join("\n"));
  ' "$ROOT/backend/apps-script/manage.html" "$OUT/manage_inline.js"
  node --check "$OUT/manage_inline.js"
  echo "  ok backend sources parse"
  echo
  node "$ROOT/tests/test_backend_validation.js"
  echo
else
  echo "note: Node not found; skipping the backend tests" >&2
fi

echo "host tests passed"

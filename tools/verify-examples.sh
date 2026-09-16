#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
find_library() {
  local name="$1" props
  for props in "$ROOT"/.arduino/user/libraries/*/library.properties; do
    if grep -qx "name=$name" "$props"; then dirname "$props"; return; fi
  done
  echo "Missing $name; run tools/setup.sh" >&2
  return 1
}
GFX="$(find_library M5GFX)"
UNIFIED="$(find_library M5Unified)"
"$ROOT/tools/build.sh" "$GFX/examples/Basic/TextLogScroll"
"$ROOT/tools/build.sh" "$UNIFIED/examples/Basic/Touch/DragDrop"
echo 'Display and touch compilation passed. Camera prerequisite remains BLOCKED; see docs/hardware-audit.md.'

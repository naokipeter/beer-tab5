#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="$ROOT/tools/arduino.sh"
"$CLI" version
"$CLI" core update-index
if ! "$CLI" core list | awk '$1 == "m5stack:esp32" && $2 == "3.3.9" { found=1 } END { exit !found }'; then
  if [[ -L "$ROOT/.arduino/data/packages" ]]; then
    echo 'Shared packages directory: install m5stack:esp32@3.3.9 in Arduino IDE first, or use a fresh project-local data directory.' >&2
    exit 1
  fi
  "$CLI" core install m5stack:esp32@3.3.9
fi
"$CLI" lib install 'M5GFX@0.2.29' --no-deps
"$CLI" lib install 'M5Unified@0.2.22' --no-deps

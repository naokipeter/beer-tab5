#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Fail explicitly if the local IDE/shared package installation has drifted.
if ! "$ROOT/tools/arduino.sh" core list | awk '$1 == "m5stack:esp32" && $2 == "3.3.9" { ok=1 } END { exit !ok }'; then
  echo 'Required core: m5stack:esp32@3.3.9. Run tools/setup.sh.' >&2
  exit 1
fi
if ! "$ROOT/tools/arduino.sh" lib list | awk '
  $1 == "M5GFX" && $2 == "0.2.29" { gfx=1 }
  $1 == "M5Unified" && $2 == "0.2.22" { unified=1 }
  $1 == "lvgl" && $2 == "9.2.2" { lv=1 }
  $1 == "ArduinoJson" && $2 == "7.4.3" { aj=1 }
  END { exit !(gfx && unified && lv && aj) }'; then
  echo 'Required libraries: M5GFX@0.2.29, M5Unified@0.2.22, lvgl@9.2.2 and ArduinoJson@7.4.3. Run tools/setup.sh.' >&2
  exit 1
fi
CHIP_VARIANT="${CHIP_VARIANT:-prev3}"
case "$CHIP_VARIANT" in prev3|postv3) ;; *) echo 'CHIP_VARIANT must be prev3 or postv3' >&2; exit 1;; esac
SKETCH="${1:-$ROOT/firmware/beer_terminal}"
NAME="$(basename "$SKETCH")"
# Preserve the pinned core's flags, then override its language standard last.
exec "$ROOT/tools/arduino.sh" compile \
  --fqbn "m5stack:esp32:m5stack_tab5:ChipVariant=$CHIP_VARIANT,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=default" \
  --build-property 'compiler.cpp.flags=-MMD -c "@{compiler.sdk.path}/flags/cpp_flags" {compiler.warning_flags} {compiler.optimization_flags} {compiler.common_werror_flags} -std=gnu++17' \
  --build-path "$ROOT/build/$CHIP_VARIANT/$NAME" "$SKETCH"

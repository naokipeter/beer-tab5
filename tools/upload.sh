#!/usr/bin/env bash
# Builds and uploads to a connected Tab5, then opens the serial monitor.
#
# The port is never hard-coded: pass TAB5_PORT, which `./tools/arduino.sh board
# list` will tell you.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

: "${TAB5_PORT:?Set TAB5_PORT to the device port, e.g. /dev/cu.usbmodem1101}"
CHIP_VARIANT="${CHIP_VARIANT:-prev3}"
case "$CHIP_VARIANT" in prev3|postv3) ;; *) echo 'CHIP_VARIANT must be prev3 or postv3' >&2; exit 1;; esac

CHIP_VARIANT="$CHIP_VARIANT" "$ROOT/tools/build.sh"

"$ROOT/tools/arduino.sh" upload \
  --fqbn "m5stack:esp32:m5stack_tab5:ChipVariant=$CHIP_VARIANT,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=default" \
  --port "$TAB5_PORT" --input-dir "$ROOT/build/$CHIP_VARIANT/beer_terminal" \
  "$ROOT/firmware/beer_terminal"

echo
echo 'Uploaded. Opening the serial monitor; USB re-enumerates, so the first'
echo 'lines may be missed — reset the device with the monitor open to see them.'
exec "$ROOT/tools/arduino.sh" monitor --port "$TAB5_PORT" --config baudrate=115200

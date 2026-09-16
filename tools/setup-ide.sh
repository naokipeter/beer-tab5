#!/usr/bin/env bash
# Installs the pinned libraries into the Arduino IDE's sketchbook.
#
# Arduino IDE reads ~/Documents/Arduino/libraries; tools/build.sh reads the
# project-local .arduino/user/libraries. The two are independent, so installing
# a dependency for the CLI does not make it available to the IDE. Run this after
# tools/setup.sh if you also build from the IDE.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

CLI="${ARDUINO_CLI:-}"
if [[ -z "$CLI" ]]; then
  if command -v arduino-cli >/dev/null; then
    CLI="$(command -v arduino-cli)"
  else
    CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
  fi
fi
[[ -x "$CLI" ]] || { echo 'Install Arduino CLI, or set ARDUINO_CLI.' >&2; exit 1; }

SKETCHBOOK="${ARDUINO_SKETCHBOOK:-$HOME/Documents/Arduino}"
[[ -d "$SKETCHBOOK" ]] || { echo "Sketchbook not found: $SKETCHBOOK" >&2; exit 1; }
echo "Installing into $SKETCHBOOK/libraries"

# Keep this list identical to tools/setup.sh.
LIBS=('M5GFX@0.2.29' 'M5Unified@0.2.22' 'lvgl@9.2.2' 'ArduinoJson@7.4.3')

for lib in "${LIBS[@]}"; do
  ARDUINO_DIRECTORIES_DATA="$ROOT/.arduino/data" \
  ARDUINO_DIRECTORIES_USER="$SKETCHBOOK" \
    "$CLI" --config-file "$ROOT/arduino-cli.yaml" lib install "$lib" --no-deps
done

echo
echo 'Done. Restart Arduino IDE so it re-reads the library index.'

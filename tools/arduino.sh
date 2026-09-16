#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export ARDUINO_DIRECTORIES_DATA="$ROOT/.arduino/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$ROOT/.arduino/downloads"
export ARDUINO_DIRECTORIES_USER="$ROOT/.arduino/user"
export ARDUINO_BUILD_CACHE_PATH="$ROOT/.arduino/cache"
CLI="${ARDUINO_CLI:-}"
if [[ -z "$CLI" ]]; then
  if command -v arduino-cli >/dev/null; then
    CLI="$(command -v arduino-cli)"
  else
    CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
  fi
fi
if [[ ! -x "$CLI" ]]; then
  echo 'Install Arduino CLI 1.1.1, or set ARDUINO_CLI to its executable.' >&2
  exit 1
fi
exec "$CLI" --config-file "$ROOT/arduino-cli.yaml" "$@"

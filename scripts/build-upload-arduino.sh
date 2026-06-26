#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PORT="${1:-${PORT:-}}"
FQBN="${FQBN:-esp32:esp32:esp32c6}"
SKETCH="$REPO_ROOT/arduino/local_device_portal/local_device_portal.ino"
BUILD_PATH="${BUILD_PATH:-/private/tmp/local-device-portal-arduino-build}"

if [[ -z "$PORT" ]]; then
  echo "Usage: $0 /dev/cu.usbmodemXXXX"
  echo
  echo "Available ports:"
  ls /dev/cu.* || true
  exit 2
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
  ARDUINO_IDE_CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
  if [[ -x "$ARDUINO_IDE_CLI" ]]; then
    ARDUINO_CLI="$ARDUINO_IDE_CLI"
  else
    echo "arduino-cli not found."
    echo "Install Arduino IDE 2 or add arduino-cli to PATH."
    exit 2
  fi
else
  ARDUINO_CLI="arduino-cli"
fi

echo "Compiling $SKETCH"
"$ARDUINO_CLI" compile \
  --fqbn "$FQBN" \
  --build-path "$BUILD_PATH" \
  "$SKETCH"

echo "Uploading to $PORT"
"$ARDUINO_CLI" upload \
  -p "$PORT" \
  --fqbn "$FQBN" \
  --input-dir "$BUILD_PATH"

echo "Done."

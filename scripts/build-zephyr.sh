#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BOARD="${BOARD:-xiao_esp32c6/esp32c6/hpcore}"
APP="${APP:-$REPO_ROOT/zephyr/local_device_portal}"

west build -p=always -b "$BOARD" "$APP"

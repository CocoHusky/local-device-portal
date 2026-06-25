#!/usr/bin/env bash
set -euo pipefail

BOARD="${BOARD:-xiao_esp32c6/esp32c6/hpcore}"
APP="${APP:-zephyr/local_device_portal}"

west build -p=always -b "$BOARD" "$APP"

#!/usr/bin/env bash
set -euo pipefail

BOARD="${BOARD:-xiao_esp32c6/esp32c6/hpcore}"
APP="${APP:-local-device-portal/zephyr/local_device_portal}"

if [ -d "local-device-portal" ] && [ -d "zephyr" ]; then
  west build -p=always -b "$BOARD" "$APP"
elif [ -f "zephyr/west.yml" ]; then
  echo "This helper should be run from the parent Zephyr workspace."
  echo "From this repo root, run:"
  echo "  cd .."
  echo "  west init -l local-device-portal/zephyr"
  echo "  west update"
  echo "  west build -p=always -b $BOARD local-device-portal/zephyr/local_device_portal"
  exit 2
else
  west build -p=always -b "$BOARD" "$APP"
fi

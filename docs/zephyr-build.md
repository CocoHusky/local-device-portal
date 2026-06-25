# Zephyr build notes

## One-time workspace setup

From the repository root:

```sh
west init -l zephyr
west update
west zephyr-export
python3 -m pip install --user -r ../zephyr/scripts/requirements.txt
```

## Build the portal app

```sh
west build -p=always -b xiao_esp32c6/esp32c6/hpcore zephyr/local_device_portal
```

Or use the helper:

```sh
BOARD=xiao_esp32c6/esp32c6/hpcore ./scripts/build-zephyr.sh
```

## App behavior

The app starts a setup AP, serves the setup portal on port 80, runs a captive DNS responder, scans local Wi-Fi networks, connects STA mode, saves credentials with Zephyr settings/NVS, and serves a dashboard page.

## Validation order

1. Confirm the board target exists in the installed Zephyr tree.
2. Confirm Wi-Fi driver support is enabled by the board.
3. Build successfully.
4. Flash device.
5. Connect to `mmWave-Setup`.
6. Open `http://192.168.4.1/`.
7. Scan networks.
8. Connect to local Wi-Fi.
9. Open dashboard.

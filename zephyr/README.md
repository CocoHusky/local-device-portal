# Zephyr

This directory contains the Zephyr port.

The Arduino sketch is still the UX reference implementation.

## App Code

```text
zephyr/local_device_portal
```

## App Architecture

```text
src/main.c              boot sequence and service startup
src/portal_config.h     shared product/network constants
src/portal_state.*      hostname, dashboard URL, handoff state
src/credential_store.*  settings/NVS-backed Wi-Fi credentials
src/wifi_manager.*      AP, STA connect, scan, Wi-Fi events
src/portal_http.*       HTTP socket server and route handling
src/portal_dns.*        captive DNS responder
src/portal_render.*     captive portal and dashboard HTML rendering
```

The modules are split so board-specific Wi-Fi behavior, storage, HTTP routes,
and UI rendering can evolve independently.

## west Manifest

```text
zephyr/west.yml
```

Pinned Zephyr revision: `v4.4.0`

## Workspace Location

Use this repository root as the `west` workspace root:

```text
/Users/<username>/Documents/GitHub/local-device-portal
```

## Target Board

Default target:

```text
xiao_esp32c6/esp32c6/hpcore
```

Use a different ESP32-C6 board name if your hardware needs it.

## Install Tools on macOS

Run:

```sh
brew install cmake ninja gperf ccache dtc python3
python3 -m pip install --user west
```

Add Python user scripts to `PATH` if `west` is not found:

```sh
export PATH="$HOME/Library/Python/3.14/bin:$PATH"
```

Install the Zephyr SDK:

```text
https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html
```

## Initialize west

Run these from the repository root:

```sh
cd /Users/<username>/Documents/GitHub/local-device-portal
west init -l zephyr
west update
west zephyr-export
python3 -m pip install --user -r deps/zephyr/scripts/requirements.txt
```

After `west update`, the upstream Zephyr tree should be here:

```text
/Users/<username>/Documents/GitHub/local-device-portal/deps/zephyr
```

## Build

Run:

```sh
cd /Users/<username>/Documents/GitHub/local-device-portal
west build -p=always \
  -b xiao_esp32c6/esp32c6/hpcore \
  zephyr/local_device_portal
```

## Flash

Connect the ESP32-C6 board over USB, then run:

```sh
cd /Users/<username>/Documents/GitHub/local-device-portal
west flash --port /dev/cu.usbmodem14401
```

List ports:

```sh
ls /dev/cu.*
```

## Monitor

```sh
screen /dev/cu.usbmodem14401 115200
```

If your Zephyr tree provides the Espressif monitor extension:

```sh
cd /Users/<username>/Documents/GitHub/local-device-portal
west espressif monitor
```

## Board Check

```sh
west boards | grep esp32c6
```

## Expected behavior

The Zephyr app should match Arduino:

- Setup AP: `mmWave-Setup`
- Setup URL: `http://192.168.4.1/`
- Captive DNS responder on UDP port 53
- HTTP setup portal on port 80
- Wi-Fi scan page
- Network selection page
- Password/connect page
- Local dashboard page
- Credential storage through Zephyr settings/NVS
- Dashboard URL target: `http://mmwave-xxxx.local/`
- Numeric IP fallback where mDNS is not available

## Troubleshooting

- If `west` is not found, add Python's user script directory to `PATH`.
- If `west update` fails, confirm the machine has internet access and GitHub access.
- If CMake cannot find Zephyr, run `west zephyr-export` from the workspace.
- If Python modules are missing, rerun `python3 -m pip install --user -r deps/zephyr/scripts/requirements.txt`.
- If the board name is unknown, run `west boards | grep esp32c6` and update the `-b` value.
- If flashing fails because the port is busy, close Serial Monitor, `screen`, or any other terminal using the USB port.
- If the setup AP does not appear after flashing, reset the board and check serial logs at `115200`.

# Zephyr

This directory contains the Zephyr port.

The Arduino sketch is still the UX reference implementation.

## App Code

```text
zephyr/local_device_portal
```

## west Manifest

```text
zephyr/west.yml
```

Pinned Zephyr revision: `v4.4.0`

## Target Board

Default target:

```text
xiao_esp32c6/esp32c6/hpcore
```

Use a different ESP32-C6 board name if your hardware needs it.

## Install on macOS

Install tools:

```sh
brew install cmake ninja gperf ccache dtc python3
python3 -m pip install --user west
```

Add Python user scripts to `PATH` if `west` is not found:

```sh
export PATH="$HOME/Library/Python/3.14/bin:$PATH"
```

Create a workspace:

```sh
mkdir -p /Users/alexburton/Documents/Codex/local-device-portal-zephyr
cd /Users/alexburton/Documents/Codex/local-device-portal-zephyr
git clone /Users/alexburton/Documents/GitHub/local-device-portal local-device-portal
west init -l local-device-portal/zephyr
west update
west zephyr-export
python3 -m pip install --user -r zephyr/scripts/requirements.txt
```

Install the Zephyr SDK:

```text
https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html
```

## Build

From the Zephyr workspace root:

```sh
cd /Users/alexburton/Documents/Codex/local-device-portal-zephyr
./local-device-portal/scripts/build-zephyr.sh
```

Or directly:

```sh
west build -p=always \
  -b xiao_esp32c6/esp32c6/hpcore \
  local-device-portal/zephyr/local_device_portal
```

## Flash

Connect the ESP32-C6 board over USB:

```sh
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
- If Python modules are missing, rerun `python3 -m pip install --user -r zephyr/scripts/requirements.txt`.
- If the board name is unknown, run `west boards | grep esp32c6` and update the `-b` value.
- If flashing fails because the port is busy, close Serial Monitor, `screen`, or any other terminal using the USB port.
- If the setup AP does not appear after flashing, reset the board and check serial logs at `115200`.

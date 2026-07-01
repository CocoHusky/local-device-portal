# Zephyr Firmware

This directory contains the Zephyr workspace manifest and firmware application
for Wifi Device.

## Application Layout

```text
zephyr/
├── west.yml
└── local_device_portal/
    ├── CMakeLists.txt
    ├── VERSION
    ├── prj.conf
    ├── sample.yaml
    ├── boards/
    │   ├── esp32_devkitc_esp32_procpu.conf
    │   ├── esp8684_devkitm.conf
    │   └── xiao_esp32c6_esp32c6_hpcore.conf
    └── src/
        ├── main.c
        ├── portal_config.h
        ├── portal_state.*
        ├── credential_store.*
        ├── wifi_manager.*
        ├── portal_http.*
        ├── portal_dns.*
        └── portal_render.*
```

Shared product behavior lives in `src/`. Board-specific configuration belongs
in `boards/`.

## Supported Boards

```text
esp32_devkitc/esp32/procpu        ESP-WROOM-32 style boards
esp8684_devkitm                   ESP32-C2 class ESP8684-DevKitM boards
xiao_esp32c6/esp32c6/hpcore       Seeed Studio XIAO ESP32-C6
```

ESP-WROOM-32 uses the classic ESP32 Xtensa toolchain. ESP32-C6 boards use the
RISC-V toolchain. ESP32-C2 class ESP8684 boards use the `esp8684_devkitm`
Zephyr board target.

Other ESP32 Zephyr board targets with Wi-Fi support can use the same app after
adding a matching file in `zephyr/local_device_portal/boards/` and verifying the
build. Examples in Zephyr include `esp32c3_devkitm`, `esp32c3_devkitc`,
`esp32c6_devkitc/esp32c6/hpcore`, `esp32s2_devkitc`, and
`esp32s3_devkitc/esp32s3/procpu`. Confirm available targets with:

```sh
west boards | grep -i esp32
```

ESP32-C2 reference pages:

```text
https://docs.zephyrproject.org/latest/boards/espressif/esp8684_devkitm/doc/index.html
https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp8684/esp8684-devkitm-1/user_guide.html
```

Some ESP-WROOM-32 modules report ESP32 chip revision v1.0. The WROOM board
configuration opts into that revision with
`CONFIG_ESP32_USE_UNSUPPORTED_REVISION=y`.

## New Computer Setup

Run these from a clean macOS machine.

1. Install host tools:

```sh
brew install cmake ninja gperf ccache dtc python3
python3 -m pip install --user west
export PATH="$HOME/.local/bin:$HOME/Library/Python/3.14/bin:$PATH"
```

Add the same `PATH` export to your shell profile if `west` or `esptool` is not
found in new terminal windows.

2. Install the Zephyr SDK.

Download and install the macOS Zephyr SDK that matches this workspace. The
current local setup uses:

```text
~/zephyr-sdk-0.17.4
```

After installing, enable the ESP-WROOM-32 Xtensa toolchain:

```sh
cd ~/zephyr-sdk-0.17.4
./setup.sh -t xtensa-espressif_esp32_zephyr-elf
```

Verify the WROOM compiler exists:

```sh
ls ~/zephyr-sdk-0.17.4/xtensa-espressif_esp32_zephyr-elf/bin/xtensa-espressif_esp32_zephyr-elf-gcc
```

3. Initialize the Zephyr workspace:

```sh
cd /path/to/local-device-portal
west init -l zephyr
west update
west zephyr-export
python3 -m pip install --user -r deps/zephyr/scripts/requirements.txt
west blobs fetch hal_espressif
python3 -m pip install --user "esptool>=5.2.0"
export PATH="$HOME/.local/bin:$HOME/Library/Python/3.14/bin:$PATH"
```

Generated workspace directories include:

```text
deps/
modules/
tools/
bootloader/
build*/
```

These are generated workspace files and should not be committed.

## Build

Use a board-specific build directory. This avoids stale build state when
switching between ESP32-C6 and ESP-WROOM-32.

Build ESP-WROOM-32:

```sh
cd /path/to/local-device-portal
export PATH="$HOME/.local/bin:$HOME/Library/Python/3.14/bin:$PATH"

west build -p=always \
  -d build-wroom \
  -b esp32_devkitc/esp32/procpu \
  zephyr/local_device_portal
```

Build XIAO ESP32-C6:

```sh
cd /path/to/local-device-portal
export PATH="$HOME/.local/bin:$HOME/Library/Python/3.14/bin:$PATH"

west build -p=always \
  -d build-xiao-c6 \
  -b xiao_esp32c6/esp32c6/hpcore \
  zephyr/local_device_portal
```

Build ESP32-C2 class ESP8684-DevKitM:

```sh
cd /path/to/local-device-portal
export PATH="$HOME/.local/bin:$HOME/Library/Python/3.14/bin:$PATH"

west build -p=always \
  -d build-esp8684 \
  -b esp8684_devkitm \
  zephyr/local_device_portal
```

## Flash ESP-WROOM-32

List serial ports:

```sh
ls /dev/cu.*
```

Typical WROOM USB serial port:

```text
/dev/cu.usbserial-0001
```

Flash:

```sh
west flash -d build-wroom -- --esp-device /dev/cu.usbserial-0001
```

If flashing does not start, hold `BOOT`, run the flash command, then release
`BOOT` when writing begins.

## Monitor

```sh
screen /dev/cu.usbserial-0001 115200
```

Exit `screen` with `Ctrl-A`, then `K`, then `Y`.

## Expected Behavior

After flashing, serial logs should show the setup portal starting. Join:

```text
WifiDevice-XXXXXX
```

The suffix is derived from the device identity, so each board has a distinct
setup network.

Then open:

```text
http://192.168.4.1/
```

The portal should provide:

- Captive setup page
- Zephyr-safe Wi-Fi entry without running a live scan while the setup AP is serving clients
- Password entry
- AP-STA connection using Zephyr's separate AP and STA Wi-Fi interfaces
- Saved credentials through Zephyr settings/NVS
- Dashboard URL: `http://wifi-device-xxxxxx.local/`
- Numeric IP fallback

Saved Wi-Fi credentials are stored in flash/NVS/settings and are not encrypted
unless platform security is enabled.

The Zephyr implementation intentionally does not run an interactive Wi-Fi scan
from `/scan` while a phone is connected to the setup AP. On ESP32 AP-STA this can
retune the shared radio and drop the setup client. This matches the official
Zephyr AP-STA sample behavior: enable AP mode first, start DHCP on the AP, then
connect the STA interface to a known SSID.

## Troubleshooting

- `west` not found: add `$HOME/.local/bin` to `PATH`.
- `esptool>=5.0.2 not found`: install `esptool` and ensure `$HOME/.local/bin` is on `PATH`.
- Missing Espressif blobs: run `west blobs fetch hal_espressif`.
- `ESP_IDF_PATH is not set`: run `west update hal_espressif`.
- Missing `xtensa-espressif_esp32_zephyr-elf-gcc`: run `~/zephyr-sdk-0.17.4/setup.sh -t xtensa-espressif_esp32_zephyr-elf`.
- ESP32 revision v1.0 warning: the WROOM board config already enables `CONFIG_ESP32_USE_UNSUPPORTED_REVISION=y`.
- ESP32 runner rejects `--port`: use `west flash -d build-wroom -- --esp-device /dev/cu.usbserial-0001`.
- Unknown board name: run `west boards | grep -i esp32`.

# Arduino Firmware

The Arduino sketch is the reference portal firmware.

## Portal Code

```text
arduino/local_device_portal/
├── local_device_portal.ino   # setup/loop, routes, pages, AP/STA flow
├── portal_config.h           # constants and shared runtime state
├── portal_utils.h            # HTML, JSON, and signal helpers
├── wifi_portal.h             # hostname, mDNS, and saved Wi-Fi credentials
├── portal_steps.h            # setup progress indicator
├── portal_status_card.h      # status card renderer
├── portal_actions.h          # reset/settings action renderer
└── portal_forms.h            # password field UI helpers
```

The main `.ino` still owns the firmware flow. Smaller portal helpers are split into Arduino tabs so the sketch is easier to read without changing runtime behavior.

## Install

1. Download and install Arduino IDE 2 from `https://www.arduino.cc/en/software`.
2. Open Arduino IDE.
3. Go to `Arduino IDE > Settings`.
4. In `Additional boards manager URLs`, add:

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

5. Open `Tools > Board > Boards Manager`.
6. Search for `esp32`.
7. Install `esp32 by Espressif Systems`.

Use version `3.1.0` or newer. This project has been compiled with ESP32 Arduino core `3.3.10`.

## Build and Upload from Arduino IDE

1. In Arduino IDE, open:

```text
arduino/local_device_portal/local_device_portal.ino
```

2. Connect the ESP32-C6 board over USB.
3. Select the board from `Tools > Board`.

Recommended board choices:

- `ESP32C6 Dev Module` for a generic ESP32-C6 board.
- `XIAO_ESP32C6` for a Seeed Studio XIAO ESP32C6.

4. Select the USB port from `Tools > Port`.

On macOS it usually looks like `/dev/cu.usbmodem...`.

5. Click `Upload`.
6. Open `Tools > Serial Monitor`.
7. Set the baud rate to `115200`.

After boot, the device should print setup information similar to:

```text
AP ON
SSID: WifiDevice-XXXXXX
Setup URL: http://192.168.4.1/
```

## Build and Upload from macOS Terminal

From the repository root:

```sh
./scripts/build-upload-arduino.sh /dev/cu.usbmodem14401
```

Use your actual port if it is different:

```sh
ls /dev/cu.*
```

The script uses:

```text
FQBN: esp32:esp32:esp32c6
Sketch: arduino/local_device_portal/local_device_portal.ino
```

## Use the Portal

Connect your computer or phone to:

```text
Network: WifiDevice-XXXXXX
Password: wifi-device-setup
```

If a captive portal window appears, use it. If it does not appear, open:

```text
http://192.168.4.1/
```

Then:

1. Click `Scan networks`.
2. Select your home or lab Wi-Fi network.
3. Enter the Wi-Fi password.
4. Click `Connect`.
5. Open the dashboard URL shown by the success page.

Preferred dashboard URL:

```text
http://wifi-device-xxxxxx.local/
```

Use the numeric IP on the success page if `.local` does not resolve.

## Reset Wi-Fi

Click `Reset saved Wi-Fi` in the portal or dashboard. The board restarts and returns to `WifiDevice-XXXXXX`.

## Development Mode

The sketch currently clears saved Wi-Fi once per newly compiled upload:

```cpp
const bool CLEAR_WIFI_ON_NEW_UPLOAD = true;
```

Set it to `false` for production behavior.

## Dependencies

Uses only ESP32 Arduino core libraries:

- `WiFi.h`
- `WebServer.h`
- `DNSServer.h`
- `Preferences.h`
- `ESPmDNS.h`

## Troubleshooting

- If upload fails because the port is busy, close Arduino Serial Monitor or any other serial terminal.
- If no setup Wi-Fi appears, reset the board and watch Serial Monitor at `115200`.
- If `http://192.168.4.1/` does not load, confirm your computer or phone is connected to `WifiDevice-XXXXXX`.
- If `.local` does not work after setup, use the backup IP address from the success page.

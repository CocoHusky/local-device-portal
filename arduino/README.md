# Arduino MVP

The Arduino implementation is the first working base for the local-device portal.

## Sketch

```text
arduino/local_device_portal/local_device_portal.ino
```

## Upload

1. Open the sketch in Arduino IDE.
2. Select an ESP32-C6 board.
3. Upload.
4. Open Serial Monitor at `115200` baud.

## Setup flow

1. Join Wi-Fi network `mmWave-Setup`.
2. Password: `focusfetch`.
3. Use the captive portal popup, or open `http://192.168.4.1/`.
4. Scan networks.
5. Pick local Wi-Fi.
6. Enter password.
7. Open dashboard at `http://mmwave-xxxx.local/`.

## Development behavior

The sketch has development mode enabled:

```cpp
const bool CLEAR_WIFI_ON_NEW_UPLOAD = true;
```

This clears old saved Wi-Fi once per newly compiled upload. Set it to `false` when the device should remember Wi-Fi across firmware updates.

## Current dependencies

Uses only ESP32 Arduino core libraries:

- `WiFi.h`
- `WebServer.h`
- `DNSServer.h`
- `Preferences.h`
- `ESPmDNS.h`

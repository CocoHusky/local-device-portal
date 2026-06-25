# local-device-portal

Browser-based setup portal for ESP32 sensor devices, enabling Wi-Fi provisioning and local dashboards without a native app or extra downloads.

## What this is

`local-device-portal` is a reusable captive-portal and local-dashboard base for Wi-Fi devices. The first target is an ESP32-C6 mmWave sensor, with Arduino as the working MVP path and Zephyr planned next.

The intended user experience is simple:

1. Connect to the device setup Wi-Fi: `mmWave-Setup`
2. Open the captive setup page or go to `http://192.168.4.1/`
3. Pick local Wi-Fi and enter the password
4. Use the local dashboard at `http://mmwave-xxxx.local/`

The numeric IP is treated as a backup only.

## Repository layout

```text
.
├── arduino/
│   └── local_device_portal/
│       └── local_device_portal.ino
├── docs/
│   ├── architecture.md
│   └── zephyr-plan.md
├── AGENTS.md
├── MVPDETAILS.md
├── README.md
└── .gitignore
```

## Arduino MVP

The Arduino sketch provides:

- Setup AP: `mmWave-Setup`
- Captive-portal-safe Wi-Fi setup pages
- Step 1/2/3 provisioning flow
- Wi-Fi scan with signal bars
- Password show/hide toggle using inline SVG icons
- Saved Wi-Fi credentials using NVS `Preferences`
- Development mode that clears old saved Wi-Fi on each new upload
- Local dashboard using mDNS: `http://mmwave-xxxx.local/`
- IP fallback for routers/devices where mDNS is unreliable

## Quick start

1. Open `arduino/local_device_portal/local_device_portal.ino` in Arduino IDE.
2. Select an ESP32-C6 board, such as ESP32C6 Dev Module or Seeed XIAO ESP32C6 if installed.
3. Set Serial Monitor to `115200`.
4. Upload.
5. Connect to Wi-Fi network `mmWave-Setup` with password `focusfetch`.
6. Open `http://192.168.4.1/` if the captive portal does not appear automatically.

## Development note

The sketch currently has:

```cpp
const bool CLEAR_WIFI_ON_NEW_UPLOAD = true;
```

That clears saved Wi-Fi once per newly compiled upload so old test credentials do not keep affecting development. Set it to `false` later for production behavior.

## Planned Zephyr path

Zephyr will be added after the Arduino flow is stable. The goal is to preserve the same user experience and endpoint model while replacing Arduino-specific networking with Zephyr services.

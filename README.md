# local-device-portal

Browser-based setup portal for ESP32 sensor devices, enabling Wi-Fi provisioning and local dashboards without a native app or extra downloads.

## Start Here

Arduino is the working MVP. Zephyr is the port.

```text
Arduino setup: arduino/README.md
Zephyr setup:  zephyr/README.md
```

## User Flow

1. Connect to the device setup Wi-Fi: `mmWave-Setup`
2. Open the captive setup page or go to `http://192.168.4.1/`
3. Pick local Wi-Fi and enter the password
4. Use the local dashboard at `http://mmwave-xxxx.local/`

The numeric IP is treated as a backup only.

## Repository layout

```text
.
├── arduino/
│   ├── README.md
│   └── local_device_portal/
│       └── local_device_portal.ino
├── scripts/
│   ├── build-upload-arduino.sh
│   └── build-zephyr.sh
├── zephyr/
│   ├── README.md
│   ├── west.yml
│   └── local_device_portal/
├── docs/
│   ├── architecture.md
│   └── zephyr-plan.md
├── AGENTS.md
├── MVPDETAILS.md
├── README.md
└── .gitignore
```

## Arduino

```text
arduino/README.md
```

Firmware:

```text
arduino/local_device_portal/local_device_portal.ino
```

Build and upload from macOS:

```sh
./scripts/build-upload-arduino.sh /dev/cu.usbmodem14401
```

## Zephyr

```text
zephyr/README.md
```

Build from a Zephyr `west` workspace:

```sh
./scripts/build-zephyr.sh
```

# local-device-portal

Browser-based setup portal for ESP32 sensor devices, enabling Wi-Fi provisioning and local dashboards without a native app or extra downloads.

## Start Here

Arduino is the working MVP. Zephyr is the port.

```text
Whole repo: README.md
Arduino:    arduino/README.md
Zephyr:     zephyr/README.md
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

Arduino is the working firmware path.

```text
arduino/README.md
```

Working firmware:

```text
arduino/local_device_portal/local_device_portal.ino
```

## Zephyr

Zephyr is the active porting path.

```text
zephyr/README.md
```

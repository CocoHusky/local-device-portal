# local-device-portal

Browser-based setup portal for local ESP32 devices. The firmware provides Wi-Fi
provisioning and a local dashboard without requiring a native app, cloud service,
or manual DHCP address lookup.

## User Flow

1. Join the device setup network: `mmWave-XXXXXX`.
2. Open the captive setup page or visit `http://192.168.4.1/`.
3. Select a local Wi-Fi network and enter the password.
4. Use the dashboard at `http://mmwave-xxxxxx.local/`.

The numeric DHCP address remains available as a fallback when mDNS is not
available on the local network.

## Firmware Targets

The Arduino firmware is the current reference behavior. The Zephyr firmware is
the shared multi-board implementation path and should preserve the same setup
flow, routes, and dashboard handoff behavior.

```text
arduino/local_device_portal/      Arduino firmware
zephyr/local_device_portal/       Zephyr firmware application
```

Current Zephyr board targets:

```text
esp32_devkitc/esp32/procpu        ESP-WROOM-32 style boards
xiao_esp32c6/esp32c6/hpcore       Seeed Studio XIAO ESP32-C6
```

## Repository Layout

```text
.
├── arduino/                      Arduino firmware and notes
├── zephyr/                       Zephyr app manifest and firmware app
│   ├── west.yml                  Zephyr workspace manifest
│   └── local_device_portal/      Zephyr application
├── docs/                         Product architecture and roadmap
├── .github/workflows/            Build automation
├── LICENSE
├── README.md
└── .gitignore
```

## Zephyr Workspace Model

This repository is a Zephyr application repository. Running `west update` from
the repository root creates workspace dependencies beside the product code:

```text
deps/
modules/
tools/
bootloader/
build/
```

Those directories are generated workspace content and are intentionally ignored
by Git. Product code stays in `arduino/`, `zephyr/local_device_portal/`, and
`docs/`.

## Documentation

```text
zephyr/README.md                  Zephyr setup, build, flash, monitor
arduino/README.md                 Arduino workflow
docs/architecture.md              Product architecture
docs/zephyr-plan.md               Zephyr implementation roadmap
```

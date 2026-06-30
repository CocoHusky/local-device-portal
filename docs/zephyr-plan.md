# Zephyr Roadmap

## Goal

Provide one Zephyr firmware application for supported ESP32 boards while
preserving the browser-first setup flow.

## Target Flow

```text
First-run setup:
  Join mmWave-XXXXXX
  Open captive setup page or http://192.168.4.1/
  Select Wi-Fi
  Enter password
  Connect

Daily use:
  Open http://mmwave-xxxxxx.local/
  Use numeric IP as fallback
```

## Application Layout

```text
zephyr/local_device_portal/
├── CMakeLists.txt
├── VERSION
├── prj.conf
├── sample.yaml
├── boards/
│   ├── esp32_devkitc_esp32_procpu.conf
│   └── xiao_esp32c6_esp32c6_hpcore.conf
└── src/
    ├── main.c
    ├── portal_config.h
    ├── portal_state.c
    ├── portal_state.h
    ├── credential_store.c
    ├── credential_store.h
    ├── wifi_manager.c
    ├── wifi_manager.h
    ├── portal_http.c
    ├── portal_http.h
    ├── portal_dns.c
    ├── portal_dns.h
    ├── portal_render.c
    └── portal_render.h
```

## Board Strategy

Shared behavior belongs in `src/`. Board differences belong in
`boards/<board>.conf` or devicetree overlays when needed.

Supported targets:

```text
xiao_esp32c6/esp32c6/hpcore
esp32_devkitc/esp32/procpu
```

## Milestones

### 1. Build Health

- Keep `sample.yaml` aligned with supported boards.
- Build XIAO ESP32-C6 first.
- Build ESP-WROOM-32 after the Xtensa toolchain is installed.

### 2. Setup AP

- Start AP as `mmWave-XXXXXX`.
- Assign `192.168.4.1`.
- Serve setup page in a normal browser.

### 3. Captive DNS

- Return `192.168.4.1` for DNS requests while setup AP is active.
- Preserve Android, iOS/macOS, and Windows captive probe routes.

### 4. Wi-Fi Provisioning

- Scan local Wi-Fi networks.
- Render network list with security and signal state.
- Connect as STA.
- Store credentials in settings/NVS.

### 5. Dashboard

- Serve dashboard on the local network.
- Prefer `http://mmwave-xxxxxx.local/`.
- Show numeric IP as fallback.

### 6. Handoff

- Let the user leave setup and open the dashboard.
- Stop setup AP after the success page has been delivered.

## Compatibility Risks

- AP+STA behavior can vary between ESP32-C6 and classic ESP32.
- Captive portal mini-browsers behave differently across platforms.
- mDNS reliability depends on the local network.
- ESP32-C6 and ESP-WROOM-32 use different CPU toolchains.

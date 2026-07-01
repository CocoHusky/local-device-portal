# Architecture

## Purpose

This project is a browser-first provisioning layer for local Wi-Fi devices. It is designed to reduce or eliminate the need for a native setup app.

## Core components

```text
Device boot
├── Start setup AP
│   ├── SSID: WifiDevice-XXXXXX
│   ├── IP: 192.168.4.1
│   └── DNS wildcard points clients to setup page
├── Serve setup pages
│   ├── Scan Wi-Fi
│   ├── Pick network
│   ├── Enter password
│   └── Connect to local Wi-Fi
├── Start local dashboard
│   ├── mDNS hostname: wifi-device-xxxx.local
│   └── Backup IP: router-assigned DHCP address
└── Optional handoff
    ├── Stop setup AP
    └── Host reconnects to local Wi-Fi and opens dashboard
```

## Why captive-portal-safe pages

Captive portal mini-browsers are inconsistent. Some restrict JavaScript, caching, redirects, popups, or background fetches. For that reason, the MVP uses normal HTML forms and full page reloads for setup-critical actions.

This makes the flow less flashy, but much more reliable.

## Networking model

### Setup AP

The device always starts a setup AP during development.

```text
SSID: WifiDevice-XXXXXX
Password: wifi-device-setup
IP: 192.168.4.1
```

The setup AP is used for first-run provisioning and recovery.

### STA local network

After the user submits local Wi-Fi credentials, the ESP32 connects in station mode while the setup AP remains active. This gives the browser enough time to show the success page.

### Handoff

On the success page, the user can choose to go to the dashboard. The device then schedules setup AP shutdown. The host computer or phone should fall back to the normal Wi-Fi, then open the local dashboard URL.

## Discovery model

Primary dashboard address:

```text
http://wifi-device-xxxx.local/
```

The suffix is derived from the ESP32 MAC address to avoid collisions across multiple devices.

Fallback address:

```text
http://192.168.x.x/
```

The fallback IP is shown because mDNS can fail on some networks.

## Storage model

Credentials are stored in ESP32 NVS through Arduino `Preferences`:

```text
namespace: wifi
keys:
  ssid
  pass
```

During development, the sketch can clear credentials once per newly compiled upload using a build ID.

## Route model

User-facing pages:

| Route | Purpose |
|---|---|
| `/` | Setup page on AP, dashboard on local mDNS/IP |
| `/setup` | Setup page |
| `/scan` | Blocking scan page with network list |
| `/pick` | Password page for selected network |
| `/connect` | Form POST to connect local Wi-Fi |
| `/handoff` | Stop setup AP and open dashboard |
| `/dashboard` | Dashboard page |
| `/manual` | Manual network entry |
| `/forget` | Clear saved Wi-Fi and restart setup |
| `/status` | Debug/status JSON |

Captive portal probe routes:

| Route | Platform |
|---|---|
| `/generate_204` | Android |
| `/gen_204` | Android alternate |
| `/hotspot-detect.html` | iOS/macOS |
| `/library/test/success.html` | Apple alternate |
| `/connecttest.txt` | Windows |
| `/ncsi.txt` | Windows |
| `/redirect` | Generic |

## Zephyr migration strategy

Keep the same routes and UI flow. Replace Arduino-specific pieces with Zephyr equivalents:

| Arduino MVP | Zephyr equivalent |
|---|---|
| `WiFi.h` | Zephyr Wi-Fi management APIs |
| `WebServer.h` | Zephyr HTTP server or custom HTTP service |
| `DNSServer.h` | UDP DNS responder |
| `Preferences` | Zephyr settings/NVS |
| `ESPmDNS` | Zephyr mDNS or fallback discovery |

The Zephyr implementation should first preserve behavior before optimizing architecture.

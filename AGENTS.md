# AGENTS.md

Guidance for coding agents working in this repository.

## Project intent

Build a reusable local-device setup portal that minimizes the need for native apps, extra downloads, or cloud infrastructure. The first supported target is an ESP32-C6 device using Arduino. Zephyr support will be added after the Arduino MVP is stable.

## Product principles

- First-run setup should work from a captive portal.
- The main daily dashboard should not require typing a random DHCP IP.
- Prefer `http://mmwave-xxxx.local/` through mDNS, with numeric IP as a backup.
- Avoid native apps unless a browser-based path is not sufficient.
- Captive portal pages should be simple and robust.
- Avoid relying on modern SPA behavior inside captive mini-browsers.
- Prefer normal HTML forms and full page loads for provisioning.

## Current implementation

The Arduino MVP lives at:

```text
arduino/local_device_portal/local_device_portal.ino
```

It provides:

- Setup AP at `mmWave-Setup`
- Setup page at `http://192.168.4.1/`
- Captive portal probe handlers
- Wi-Fi scan and network selection
- Password entry with show/hide icon
- Local dashboard through `http://mmwave-xxxx.local/`
- IP fallback
- Reset saved Wi-Fi action

## Development rules

- Keep the Arduino sketch self-contained until the UX stabilizes.
- Do not add external Arduino libraries unless clearly justified.
- Do not store real Wi-Fi credentials in the repo.
- Keep captive portal forms functional without JavaScript.
- JavaScript is acceptable for small progressive enhancements only.
- Any Zephyr implementation should preserve the same URLs, flow, and user-visible behavior.

## Testing checklist

Before considering a change stable, test:

1. Fresh upload with no saved Wi-Fi.
2. Captive portal opens after joining `mmWave-Setup`.
3. `http://192.168.4.1/` works in a normal browser.
4. Scan lists nearby networks.
5. Secure networks show a lock; open networks show blank space.
6. Password hide/show button works.
7. Correct Wi-Fi credentials connect successfully.
8. Wrong password returns to the password page with a useful error.
9. Success page shows `.local` dashboard URL and backup IP.
10. Go-to-dashboard handoff shuts down setup AP and loads the dashboard after the host reconnects to local Wi-Fi.
11. Reset saved Wi-Fi clears NVS and restarts setup.

## Future Zephyr work

Create a Zephyr implementation only after the Arduino flow is proven. Keep the same conceptual modules:

- Wi-Fi/AP+STA manager
- Captive DNS handler
- HTTP route handlers
- Credential storage
- mDNS/local discovery or equivalent
- Dashboard page renderer

Prefer small, reviewable changes. Do not replace the user flow unless explicitly requested.

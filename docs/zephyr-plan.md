# Zephyr Plan

## Goal

Port the proven Arduino provisioning flow to Zephyr without changing the user experience.

## Do not start here

The Arduino path is the UX prototype and MVP. Zephyr work should begin only after the following are stable:

- Captive portal opens reliably enough for setup
- Full browser fallback works at `192.168.4.1`
- Wi-Fi scan and connect flow works
- `.local` dashboard works where supported
- Backup IP is available
- Handoff behavior is understandable to users

## Target behavior

Zephyr should preserve:

```text
First-run setup:
  Connect to mmWave-Setup
  Open captive setup page or 192.168.4.1
  Pick Wi-Fi
  Enter password
  Connect

Daily use:
  Open http://mmwave-xxxx.local/
  Use numeric IP only as fallback
```

## Proposed Zephyr modules

```text
zephyr/
├── app/
│   ├── src/
│   │   ├── main.c
│   │   ├── portal_http.c
│   │   ├── portal_http.h
│   │   ├── portal_dns.c
│   │   ├── portal_dns.h
│   │   ├── wifi_manager.c
│   │   ├── wifi_manager.h
│   │   ├── credential_store.c
│   │   ├── credential_store.h
│   │   ├── dashboard.c
│   │   └── dashboard.h
│   ├── prj.conf
│   └── CMakeLists.txt
└── README.md
```

## Milestones

### Phase 1: Skeleton

- Add Zephyr app directory
- Confirm board config for ESP32-C6 target
- Build minimal HTTP response
- Verify serial logs and boot flow

### Phase 2: Setup AP

- Start AP at `mmWave-Setup`
- Assign `192.168.4.1`
- Serve setup page
- Confirm browser can open page manually

### Phase 3: Captive DNS

- Add UDP DNS responder
- Return `192.168.4.1` for all queries while setup AP is active
- Add captive probe routes

### Phase 4: Wi-Fi scan/connect

- Scan local Wi-Fi networks
- Render network list with signal strength
- Connect STA while AP remains active
- Store credentials in settings/NVS

### Phase 5: Local dashboard

- Serve dashboard on local network
- Add mDNS if supported
- Keep numeric IP fallback

### Phase 6: Handoff

- Stop setup AP after user requests dashboard handoff
- Validate reconnect behavior from macOS, iOS, Android, and Windows

## Risks

- AP+STA behavior on ESP32-C6 can be more fragile in Zephyr than Arduino.
- Captive portal mini-browsers are inconsistent.
- mDNS support may differ from Arduino.
- Zephyr HTTP server APIs may require more explicit socket handling.

## Rule for porting

Do not try to make Zephyr more advanced than the Arduino MVP at first. Match the working flow, then refactor.

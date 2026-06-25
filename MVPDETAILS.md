# MVP Details

## Goal

Create an app-free setup portal for local Wi-Fi devices. A user should be able to configure a device with only a browser and then access a local dashboard without installing a native app.

## MVP target

Initial target device:

- ESP32-C6
- Arduino framework
- Captive setup AP
- Local HTTP dashboard
- mDNS dashboard hostname

Future target:

- Zephyr implementation for the same device and user flow

## User story

As a user, I want to set up a Wi-Fi sensor without downloading an app, so I can connect it to my local network and open its dashboard from a browser.

## Flow

### Step 1: Setup Wi-Fi

User connects to the device AP:

```text
SSID: mmWave-Setup
Password: focusfetch
Setup URL: http://192.168.4.1/
```

The captive portal should open automatically when possible. If not, the user can manually open the setup URL.

### Step 2: Pick local network

The setup page scans nearby Wi-Fi networks and shows:

- SSID
- Signal strength label
- Signal bar
- Lock marker for secured networks

It should not show technical details like channel number in the user-facing list.

### Step 3: Connect and open dashboard

After credentials are submitted, the device joins local Wi-Fi and shows:

- Primary dashboard URL: `http://mmwave-xxxx.local/`
- Backup IP address: `http://192.168.x.x/`

When the user chooses to go to the dashboard, the setup AP shuts down and the host device should reconnect to the local Wi-Fi. The page explains that this can take up to 30 seconds.

## User interface requirements

- Short wording
- Mobile-first layout
- Step 1/2/3 progress indicator
- Device Status card at the top
- Reset Wi-Fi action small and near the bottom
- Reset Wi-Fi hidden when there is nothing saved
- Password field hidden by default
- Modern grey SVG eye / eye-off toggle
- Captive-portal-safe forms

## Technical requirements

- Serve HTTP on port 80
- Use `DNSServer` to direct captive portal DNS to `192.168.4.1`
- Use `WebServer` for setup/dashboard pages
- Use `Preferences` for saved Wi-Fi credentials
- Use `ESPmDNS` for `.local` dashboard discovery
- Avoid external Arduino dependencies for the MVP
- Keep numeric IP as fallback only
- Do not require cloud infrastructure
- Do not require a mobile app

## Known limitations

- Captive portal mini-browsers can be inconsistent across OSes.
- mDNS can be inconsistent on some Windows or restricted networks.
- Some phones may not immediately return to local Wi-Fi after the setup AP shuts down.
- The current Arduino MVP is intentionally optimized for proving UX before porting to Zephyr.

## Definition of done for Arduino MVP

- Fresh upload clears prior saved Wi-Fi in development mode.
- Captive portal opens or manual `192.168.4.1` works.
- Network scan works from the setup page.
- User can choose a network and connect.
- Dashboard is accessible from `mmwave-xxxx.local` after connection.
- Backup IP is visible when needed.
- Reset saved Wi-Fi works.
- No native app is required.

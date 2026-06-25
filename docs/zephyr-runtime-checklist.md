# Zephyr runtime checklist

Use this after the build succeeds and the board is flashed.

## Serial

Expected boot logs:

```text
Local Device Portal Zephyr MVP starting
setup URL: http://192.168.4.1/
setup AP started: mmWave-Setup / 192.168.4.1
HTTP server listening on port 80
DNS captive responder listening on port 53
```

## Setup AP

- `mmWave-Setup` appears in Wi-Fi list.
- Password is `focusfetch`.
- Client receives a `192.168.4.x` address.
- Browser can open `http://192.168.4.1/`.

## Portal

- Step 1 page opens.
- Scan page returns networks.
- Network list shows lock or blank space.
- Network list shows signal label and bar.
- Manual entry page works.
- Wrong password shows a useful error.
- Correct password shows success page.

## Dashboard

- Success page shows dashboard target.
- Handoff stops setup AP.
- Browser can open dashboard after reconnecting to local Wi-Fi.

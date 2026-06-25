# Zephyr

This directory contains the Zephyr port of the local-device portal.

## App path

```text
zephyr/local_device_portal
```

## Target board

Primary target from the existing ESP32-C6 work:

```text
xiao_esp32c6/esp32c6/hpcore
```

## Build

From a Zephyr workspace with `west` initialized:

```sh
west build -p=always \
  -b xiao_esp32c6/esp32c6/hpcore \
  zephyr/local_device_portal
```

For another ESP32-C6 board package, use the matching board name from your installed Zephyr tree.

## Expected behavior

The Zephyr app is intended to match the Arduino MVP behavior:

- Setup AP: `mmWave-Setup`
- Setup URL: `http://192.168.4.1/`
- Captive DNS responder on UDP port 53
- HTTP setup portal on port 80
- Wi-Fi scan page
- Network selection page
- Password/connect page
- Local dashboard page
- Credential storage through Zephyr settings/NVS
- Dashboard URL target: `http://mmwave-xxxx.local/`
- Numeric IP fallback where mDNS is not available

## Implementation note

The Zephyr implementation uses raw Zephyr sockets for HTTP and DNS so the route behavior stays close to the Arduino MVP.

## Hardware validation order

ESP32-C6 AP+STA behavior can vary by Zephyr version and Espressif HAL state. Validate in this order:

1. Board name exists in the installed Zephyr tree.
2. Wi-Fi driver is enabled for that board.
3. Setup AP starts.
4. Client receives an IP from the DHCP server.
5. HTTP page opens at `192.168.4.1`.
6. Scan events return results.
7. STA connect events return success.

The Arduino sketch remains the UX reference implementation.

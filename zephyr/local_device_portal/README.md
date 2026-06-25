# local_device_portal Zephyr app

Zephyr implementation of the browser-based local-device portal.

## Build

```sh
west build -p=always -b xiao_esp32c6/esp32c6/hpcore zephyr/local_device_portal
```

## Runtime flow

1. Device starts setup AP `mmWave-Setup`.
2. Captive DNS points clients to `192.168.4.1`.
3. HTTP server serves the setup portal.
4. User scans and selects Wi-Fi.
5. Device connects STA and saves credentials through Zephyr settings.
6. Dashboard is shown at the local dashboard URL target.

## Notes

This app intentionally uses low-level Zephyr sockets for the HTTP and DNS servers so it can track the Arduino MVP route behavior closely.

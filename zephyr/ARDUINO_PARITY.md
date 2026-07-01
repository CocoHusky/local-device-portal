# Arduino-to-Zephyr Portal Parity

The Arduino sketch is the behavior reference for the local device portal.

Zephyr cannot directly copy/paste Arduino `WiFi`, `DNSServer`, or `WebServer` code because those are Arduino/Espressif C++ APIs. The Zephyr app must implement the same flow using Zephyr networking APIs.

## Behavior source of truth

Arduino flow:

```cpp
WiFi.mode(WIFI_AP_STA);
WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
WiFi.softAP(setupApSsid.c_str(), AP_PASS);
dns.start(DNS_PORT, "*", AP_IP);
server.begin();
```

Zephyr parity flow:

```text
1. Get SAP/AP Wi-Fi interface.
2. Assign AP IPv4 address 192.168.4.1/24.
3. Enable AP mode with SSID WifiDevice-xxxxxx and password wifi-device-setup.
4. Start DHCP server from 192.168.4.2.
5. Start captive DNS responder on 0.0.0.0:53.
6. Start HTTP server on 0.0.0.0:80.
7. Keep AP setup portal active while STA connect attempts run.
```

## Important implementation rule

Do not bind DNS or HTTP sockets to a Zephyr Wi-Fi interface name unless there is a proven board-specific reason.

Arduino's `DNSServer` and `WebServer` behavior is global listener behavior after the AP owns `192.168.4.1`. Zephyr should mirror that by binding DNS and HTTP to `INADDR_ANY` / `0.0.0.0`.

Binding sockets with `SO_BINDTODEVICE` can make logs show that DNS/HTTP are listening while AP clients cannot reach the service on some ESP32 targets.

## Supported Zephyr board targets

```sh
# ESP32-WROOM / classic ESP32
west build -p=always \
  -d build-wroom \
  -b esp32_devkitc/esp32/procpu \
  zephyr/local_device_portal

# Seeed XIAO ESP32-C6
west build -p=always \
  -d build-xiao-c6 \
  -b xiao_esp32c6/esp32c6/hpcore \
  zephyr/local_device_portal
```

## Test criteria

After flashing, serial logs should show:

```text
setup AP enabled: WifiDevice-xxxxxx / 192.168.4.1
setup DHCP started: 192.168.4.2
setup AP ready: WifiDevice-xxxxxx / 192.168.4.1
DNS captive responder listening on 0.0.0.0:53
HTTP server listening on 0.0.0.0:80
```

When a phone or laptop connects to the AP and opens `http://192.168.4.1/`, serial should show:

```text
HTTP GET /
```

If the client gets `169.254.x.x`, DHCP is still broken.
If the client gets `192.168.4.x` but there is no `HTTP GET /`, AP routing or socket binding is still broken.
If `HTTP GET /` appears but the browser is blank, the problem is the HTTP response/render path.

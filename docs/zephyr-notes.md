# Zephyr implementation notes

The first Zephyr implementation uses:

- Wi-Fi management requests for scan, connect, AP enable, and AP disable
- Wi-Fi management events for scan and connect results
- Zephyr sockets for the HTTP server and captive DNS responder
- Zephyr settings/NVS for saved credentials
- DHCPv4 server for setup AP clients

The app is kept close to the Arduino MVP behavior first. Refactoring into smaller modules should happen after the target board build and runtime flow are verified.

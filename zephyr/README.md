# Zephyr

Zephyr support will be added after the Arduino MVP flow is stable.

The Zephyr implementation should preserve the same user-facing behavior:

- Setup AP: `mmWave-Setup`
- Setup URL: `http://192.168.4.1/`
- Captive portal probe routes
- Step 1/2/3 provisioning flow
- mDNS or equivalent local discovery
- Dashboard primary URL: `http://mmwave-xxxx.local/`
- Numeric IP backup

See [`../docs/zephyr-plan.md`](../docs/zephyr-plan.md) for the staged plan.

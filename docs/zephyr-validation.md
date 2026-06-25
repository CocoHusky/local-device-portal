# Zephyr validation

## Build validation

The PR includes a GitHub Actions workflow and a local build helper. The expected target is:

```text
xiao_esp32c6/esp32c6/hpcore
```

## Runtime validation

The important runtime checkpoints are:

- Setup AP appears.
- Captive portal opens.
- Manual browser URL opens.
- Wi-Fi scan returns networks.
- Connect succeeds with correct credentials.
- Dashboard opens after handoff.

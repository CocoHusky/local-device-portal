# Building

```sh
west build -p=always -b xiao_esp32c6/esp32c6/hpcore zephyr/local_device_portal
```

The app is intentionally a single-file first pass in `src/main.c` so the Zephyr API surface is easy to inspect and debug before splitting into modules.

# Zephyr commands

Build:

```sh
west build -p=always -b xiao_esp32c6/esp32c6/hpcore zephyr/local_device_portal
```

Use another board by setting `BOARD`:

```sh
BOARD=esp32c6_devkitc/esp32c6/hpcore ./scripts/build-zephyr.sh
```

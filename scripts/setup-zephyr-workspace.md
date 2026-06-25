# Zephyr workspace setup

Run these from the repository root after installing `west` and the Zephyr SDK/toolchain for ESP32-C6:

```sh
west init -l zephyr
west update
west zephyr-export
python3 -m pip install --user -r ../zephyr/scripts/requirements.txt
```

Then build:

```sh
BOARD=xiao_esp32c6/esp32c6/hpcore ./scripts/build-zephyr.sh
```

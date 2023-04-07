This procedure will remove the default micropython environment.
Maybe you can undo that, but I have not tried that.

* esptool.py erase\_flash

* pio run -e SHA2017-badge

* esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default\_reset --after hard\_reset write\_flash -z --flash\_mode dio --flash\_freq 80m --flash\_size detect 0x1000 ./.pio/build/ESP32-wemos/bootloader.bin

* pio run -e SHA2017-badge -t upload

After this, you can connect a serial terminal to /dev/ttyUSB0 at 115k2 bps.

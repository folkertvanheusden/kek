KEK
---
Kek is a DEC PDP-11 (11/70) emulator capable of running UNIX-v7.

You need to retrieve the git repository with the --recursive switch for the git command:

    git clone --recursive https://github.com/folkertvanheusden/kek

To build for e.g. linux:

    mkdir build
    cd build
    cmake ..
    make

    Required:
    * libncursesw5-dev

To build for e.g. windows:

    mkdir build-win32
    cd build-win32
    cmake -DCMAKE_TOOLCHAIN_FILE=../mingw64.cmake ..
    make


To run an RK05 disk image:

    ./kek -r filename.rk -R rk05 -b 2> /dev/null


To run an RL02 disk image:

    ./kek -r filename.rk -R rl02 -b 2> /dev/null


To run an RP06 disk image:

    ./kek -r filename.rk -R rp06 -b 2> /dev/null


To run a tape image:

    ./kek -T filename.bin -b 2> /dev/null


When you run UNIX 7, you can (if your system has enough RAM - use an ESP32 with 2 MB PSRAM or more) run multi-user via the DC-11 emulation.
Note that UNIX 7 starts in single user mode first; press ctrl+d to switch to multi user (recognizable by the login-prompt).
Also make sure to configure networking ('startnet') to be able to connect (using telnet) to the DC-11 ports (TCP port 1101 upto and including 1104).


ESP32
-----
The ESP32 version needs platformio to be build.

    cd ESP32
    pio run -t upload
    pio run -t uploadfs

That should build & upload it to a connected ESP32.

Wiring of SDCARD (or use disk-images exported via NBD over wifi using nbdkit (because of older NBD protocol implementation)):
* MISO: 19
* MOSI: 23
* SCK : 18
* SS  : 5
* and of course connect VCC/GND of the SD-card

Heart beat LED:
* pin 25

Wiring of the MAX232 connection:
* TX  : 17
* RX  : 16
Note that you need to use different pins for the MAX232 connection when you want to use the PSRAM of the ESP32.


Raspberry PI PICO / RP2040
--------------------------
Wiring of SDCARD:
* MISO: 16
* MOSI: 19
* SCK : 18
* SS  : 17
* and of course connect VCC/GND of the SD-card

The RP2040 version needs platformio to be build.

    cd RP2040
    pio run

Then copy RP2040/.pio/build/BUILD\_FOR\_RP2040/firmware.uf2 to the PICO.


SHA2017-badge
-------------
This procedure will remove the default micropython environment.
Maybe you can undo that, but I have not tried that.

* esptool.py erase\_flash

* pio run -e SHA2017-badge

* esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default\_reset --after hard\_reset write\_flash -z --flash\_mode dio --flash\_freq 80m --flash\_size detect 0x1000 ./.pio/build/ESP32-wemos/bootloader.bin

* pio run -e SHA2017-badge -t upload

After this, you can connect a serial terminal to /dev/ttyUSB0 at 115k2 bps.


more info
---------

For more info: https://vanheusden.com/emulation/PDP-11/kek/


thanks
------

Thanks a lot to Neil Webber for his help and for his python PDP emulator (which allowed me to compare disassembly of runs).
Neil's emulator can be found at https://github.com/outofmbufs/python-pdp11-emulator


Kek is released under MIT license.

Folkert van Heusden

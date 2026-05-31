KEK
---

Kek is a DEC PDP-11 (11/70) emulator capable of running e.g. UNIX-v7 and BSD 2.11.

You need to retrieve the git repository with the --recursive switch for the git command:

    git clone --recursive https://github.com/folkertvanheusden/kek

To build for e.g. linux:

    mkdir build
    cd build
    cmake ..
    make

    Required:
    * libncursesw5-dev, cmake, build-essential, pkg-config, libjansson-dev
    Optional:
    * libsdl3-dev, libimgui-dev

To build for e.g. windows:

    mkdir build-win32
    cd build-win32
    cmake -DCMAKE_TOOLCHAIN_FILE=../mingw64.cmake ..
    make


To run an RK05 disk image:

    ./kek -r filename.rk -R rk05 -b rk05

Replace rk05 by rl02 or rp06 for variations.

To run a tape image:

    ./kek -T filename.bin -b tm11


When you run UNIX 7, you can (if your system has enough RAM - use a micrcontroller with 2 MB PSRAM or more) run multi-user via the DZ-11 emulation.
Note that UNIX 7 starts in single user mode first; press ctrl+d to switch to multi user (recognizable by the login-prompt).
Also make sure to configure networking ('startnet') to be able to connect (using telnet) to the DC-11 ports (TCP port 1101 upto and including 1104).

If using windows, run Kek with -c x where x is some portnumber. You can then telnet to that port to control the emulator/emulated system.


ESP32
-----
Preferably an ESP32-S3.
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

If possible, use a waveshare-esp32-s3-eth: that device can do Ethernet for the emulated system over its Ethernet port.


Raspberry PI PICO2W
-------------------
Wiring of SDCARD:
* MISO: 16
* MOSI: 19
* SCK : 18
* SS  : 17
* and of course connect VCC/GND of the SD-card

The PICO2W version needs platformio to be build.

    cd PICO2W
    pio run

Then copy PICO2W/.pio/build/BUILD\_FOR\_PICO2W/firmware.uf2 to the PICO.


more info
---------

For more info: https://vanheusden.com/emulation/PDP-11/kek/


thanks
------

Thanks a lot to Neil Webber for his help and for his python PDP emulator (which allowed me to compare disassembly of runs).
Neil's emulator can be found at https://github.com/outofmbufs/python-pdp11-emulator


## Star History

[![Star History Chart](https://api.star-history.com/chart?repos=folkertvanheusden/kek&type=date&legend=top-left)](https://www.star-history.com/?repos=folkertvanheusden%2Fkek&type=date&legend=top-left)


Kek is released under MIT license.

Folkert van Heusden

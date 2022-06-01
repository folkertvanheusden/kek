KEK
Kek might (I work occasionally on it so don't hold your breath) become a DEC PDP-11 (11/70) emulator capable of running UNIX-v5.

Run:
    make all
to build.


Required:
* libncursesw5-dev


To run a disk image:

    ./kek -R filename.rk -b 2> /dev/null

Kek emulates an RK05.


To run a tape image:

    ./kek -T filename.bin -b 2> /dev/null




Released under Apache License v2.0

Folkert van Heusden

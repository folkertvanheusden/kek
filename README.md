KEK
Kek might (I work occasionally on it so don't hold your breath) become a DEC PDP-11 emulator capable of running UNIX-v7.

Run:
    make all
to build.


Required:
* libtirpc-dev
* libncursesw5-dev


To run the test-cases:

    ./kek -m tc

... it should end in "ALL FINE".


To run a disk image:

    ./kek -R filename.rk -p offset 2> /dev/null

... where offset is a decimal(!) address to start (optional).


To run a tape image:

    ./kek -T filename.bin -p offset 2> /dev/null

... where offset is a decimal(!) address to start (optional).


Released under Apache License v2.0

Folkert van Heusden

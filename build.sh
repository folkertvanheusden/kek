#! /bin/bash

TMP=/tmp/.$$TMP$$
echo > $TMP
mkdir logs

(cd build/ && make -j kek-native > ../logs/native.log 2>&1 ; if [ $? -eq 0 ] ; then echo c++ OK ; else echo -e "c++ \e[31mFAIL\e[0m" ; rm -f $TMP ; fi)
(cd build-win32/ && make -j kek-win32 > ../logs/win32.log 2>&1 ; if [ $? -eq 0 ] ; then echo win32 OK ; else echo -e "win32 \e[31mFAIL\e[0m" ; rm -f $TMP ; fi)
(cd ESP32/ && ~/.local/bin/pio pkg update && pio run > ../logs/esp32.log 2>&1 ; if [ $? -eq 0 ] ; then echo esp32 OK ; else echo -e "esp32 \e[31mFAIL\e[0m" ; rm -f $TMP ; fi)
(cd PICO2W/ && ~/.local/bin/pio pkg update && pio run > ../logs/picow.log 2>&1 ; if [ $? -eq 0 ] ; then echo picow OK ; else echo -e "picow \e[31mFAIL\e[0m" ; rm -f $TMP ; fi)
(cd TEENSY4_1/ && ~/.local/bin/pio pkg update && pio run > ../logs/teensy4.1.log 2>&1 ; if [ $? -eq 0 ] ; then echo teensy4.1 OK ; else echo -e "teensy4.1 \e[31mFAIL\e[0m" ; rm -f $TMP ; fi)
if [ -e $TMP ] ; then
	echo -e "\e[32mAll GOOD\e[0m"
	rm -f $TMP
	exit 0
fi

echo -e "\e[31mOne or more builds failed\e[0m"
exit 1

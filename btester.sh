#! /bin/sh

pdpy11 --lst tester.mac

if [ ! -e raw_to_simh_bin ] ; then
	g++ -Ofast raw_to_simh_bin.cpp -o raw_to_simh_bin
fi

./raw_to_simh_bin tester 512 512 tester.bin

ls -la tester*

#! /bin/sh

INPUT=~/t/PDP/json
BIN=./build/kek-native

N=0
for f in ${INPUT}/*.json
do
	N=$((N+1))
	echo test ${N}, input: ${f}
	$BIN -J $f -S 8 -L cpu
done

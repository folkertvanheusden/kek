#! /bin/bash

INPUT=/home/folkert/Projects/simh-testsetgenerator/BIN
BIN=./build/kek-native

N=0
for f in ${INPUT}/*.json
do
	N=$((N+1))
	echo "test ${N}.log, input: ${f}"
	$BIN -J $f -L cpu,mmu,trace -l ${N}.log
done

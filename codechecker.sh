#! /bin/sh

. ~/bin/codechecker/venv/bin/activate

# not: clang-tidy cppcheck

rm -rf results
CodeChecker check --analyzers gcc infer --enable-all -j 1 -b "cd build && make clean && make kek" -o ./results
CodeChecker store ./results --url http://server3:8001/Kek -n Kek

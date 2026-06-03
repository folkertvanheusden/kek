#! /usr/bin/python3


print('var BOOTBASE=01000;')
print('var bootcode=[')

n = 0
fh = open('benchmark.raw', 'rb')
while True:
    b = fh.read(2)
    if len(b) == 0:
        break
    w = int.from_bytes(bytearray(b), 'little')

    print(f'0o{w:06o},', end='')
    n += 1
    if n == 8:
        print()
        n = 0
print('];')

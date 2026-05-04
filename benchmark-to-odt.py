#! /usr/bin/python3

start_offset = 0o1000
offset = start_offset
with open('benchmark.raw', 'rb') as fh:
    while True:
        bytes_ = fh.read(2)
        if len(bytes_) == 0:
            break
        value = int.from_bytes(bytes_, 'little')
        print(f'{offset:06o}/{value:06o}')
        offset += 2
print(f'{start_offset:06o}G')

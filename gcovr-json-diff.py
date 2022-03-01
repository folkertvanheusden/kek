#! /usr/bin/python3

import json
import sys

file_a = sys.argv[1]
json_a = json.load(open(file_a))

file_b = sys.argv[2]
json_b = json.load(open(file_b))

lines_a = dict()

for file_ in json_a['files']:
    filename = file_['file']

    covered_lines_a = [ x['line_number'] for x in file_['lines'] if x['count'] > 0]

    lines_a[filename] = covered_lines_a

lines_b = dict()

for file_ in json_b['files']:
    filename = file_['file']

    covered_lines_b = [ x['line_number'] for x in file_['lines'] if x['count'] > 0]

    lines_b[filename] = covered_lines_b

# see what has been covered in set b and not in a
for file_ in lines_b:
    if file_ not in lines_a:
        print(f'File {file_} only in set {file_b}')

    for line in lines_b[file_]:
        if not line in lines_a[file_]:
            print(f'Line {line} in {file_} is only covered in {file_b}')

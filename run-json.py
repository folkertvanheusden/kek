#! /usr/bin/python3

import json
from subprocess import Popen, PIPE, STDOUT
import sys

debug = True

def docmd(p, str):
    print(f'SEND {str}')
    p.stdin.write(str + "\n")
    p.stdin.flush()

    pl = None
    while True:
        line = p.stdout.readline().rstrip('\n').rstrip('\r')
        if debug:
            print(f'|{line}|')

        if line == '---':
            return pl

        pl = line

process = Popen(['./build/kek', '-d', '-L', 'emergency,debug', '-l', sys.argv[2]], stdin=PIPE, stdout=PIPE, stderr=STDOUT, close_fds=True, bufsize=1, universal_newlines=True, text=True)
docmd(process, 'marker')

test_file = sys.argv[1]
j = json.loads(open(test_file, 'rb').read())

error_nr = 0
test_nr = 0
diffs = []

for set in j:
    test_nr += 1
    if not 'id' in set:
        continue
    name = set['id']

    print(name)

    # initialize

    docmd(process, 'reset')
    before = set['before']

    before_mem = before['memory']
    for m_entry in before_mem:
        for key, val in m_entry.items():
            addr = int(key, 8)
            docmd(process, f'setmem a={addr:o} v={val & 255:o}')
            docmd(process, f'setmem a={addr + 1:o} v={val >> 8:o}')

    docmd(process, f'setpc {int(before["PC"]):o}')
    docmd(process, f'setpsw {int(before["PSW"]):o}')

    #for s in range(0, 2):
    s = 0
    for reg in range(0, 6):
        key = f'reg-{reg}.{s}'
        docmd(process, f'setreg {reg} {int(before[key]):o}')

    docmd(process, f'setstack 0 {int(before["stack-0"]):o}')
    docmd(process, f'setstack 1 {int(before["stack-1"]):o}')
    docmd(process, f'setstack 2 {int(before["stack-2"]):o}')
    docmd(process, f'setstack 3 {int(before["stack-3"]):o}')

    # invoke!
    docmd(process, 'step')

    # check

process.stdin.write(("q\n").encode('ascii'))
process.terminate()
time.sleep(0.5)
process.kill()

sys.exit(1 if error_nr > 0 else 0)

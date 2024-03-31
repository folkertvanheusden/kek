#! /usr/bin/python3

import sys

filter_pc_sp = True
counts = dict()

fh = open(sys.argv[1], 'r')

while True:
    l = fh.readline()
    if not l:
        break
    if not 'instr:' in l and not 'Error by instruction' in l:
        continue
    # R0: 151664, R1: 071310, R2: 072154, R3: 155125, R4: 025046, R5: 115566, SP: 027637, PC: 134104, PSW: 31|0|0|-n--c, instr: 130046: BITB R0,-(SP) - MMR0/1/2/3: 000000/000000/000000/000000

    if l[0:3] == 'R0:':
        i = l.find('instr:')
        l = l[i+6:]
        i = l.find(':')
        l = l[i+2:]
        i = l.find(' ')
        l = l[0:i]

        if not l in counts:
            counts[l] = [0, 0 ]

        counts[l][0] += 1

    elif l[0:20] == 'Error by instruction':
        l = l[21:]

        if filter_pc_sp and ('PC' in l or 'SP' in l or 'R7' in l or 'R6' in l):
            continue

        i = l.find(' ')
        l = l[0:i]

        if not l in counts:
            counts[l] = [0, 0 ]

        counts[l][1] += 1

output = [(c, counts[c][0], counts[c][1], round(counts[c][1] * 10000 / counts[c][0]) / 100) for c in counts]
for row in sorted(output, key=lambda x: float(x[3])):
    print(row)

#! /usr/bin/python3

import json
from machine import PDP1170
import random
import sys


class PDP1170_wrapper(PDP1170):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.reset_mem_transactions_dict()

    def reset_mem_transactions_dict(self):
        self.mem_transactions = dict()

    def get_mem_transactions_dict(self):
        return self.mem_transactions

    def physRW(self, physaddr, value=None):
        if value == None:  # read
            self.mem_transactions[physaddr] = random.randint(0, 65536)
            return super().physRW(physaddr, self.mem_transactions[physaddr])

        self.mem_transactions[physaddr] = value
        super().physRW(physaddr, value)

    def physRW_N(self, physaddr, nwords, words=None):
        temp_addr = physaddr
        if words == None:
            for i in range(nwords):
                physRW(temp_addr, random.randint(0, 65536))
                temp_addr += 2
            return super().physRW_N(physaddr, nwords)

        for w in words:
            self.mem_transactions[temp_addr] = w
            temp_addr += 2

        super().physRW_N(physaddr, nwords, words=words)

class test_generator:
    def _invoke_bp(self, a, i):
        return True

    def _run_test(self, addr, mem_setup, reg_setup, psw):
        p = PDP1170_wrapper(loglevel='DEBUG')

        for a, v in mem_setup:
            p.mmu.wordRW(a, v)

        for r, v in reg_setup:
            p.r[r] = v

        p.reset_mem_transactions_dict()

        p._syncregs()
        p.run_steps(pc=addr, steps=1)
        p._syncregs()

        return p

    def create_test(self):
        out = { }

        addr = random.randint(0, 65536) & ~3

        # TODO what is the maximum size of an instruction?
        mem_kv = []
        mem_kv.append((addr + 0, random.randint(0, 65536)))
        mem_kv.append((addr + 2, random.randint(0, 65536)))
        mem_kv.append((addr + 4, random.randint(0, 65536)))
        mem_kv.append((addr + 6, random.randint(0, 65536)))

        out['memory-before'] = dict()
        for a, v in mem_kv:
            out['memory-before'][a] = v

        reg_kv = []
        for i in range(7):
            reg_kv.append((i, random.randint(0, 65536)))
        reg_kv.append((7, addr))

        out['registers-before'] = dict()
        for r, v in reg_kv:
            out['registers-before'][r] = v

        out['registers-before']['psw'] = 0  # TODO random.randint(0, 65536)

        try:
            p = self._run_test(addr, mem_kv, reg_kv, out['registers-before']['psw'])

            out['registers-after'] = dict()
            for r, v in reg_kv:
                out['registers-after'][r] = v
            out['registers-after']['psw'] = p.psw

            out['memory-after'] = dict()
            for a, v in mem_kv:
                out['memory-after'][a] = v
            mem_transactions = p.get_mem_transactions_dict()
            for a in mem_transactions:
                out['memory-after'][a] = mem_transactions[a]

            # TODO check if mem_transactions affects I/O, then return None

            return out

        except Exception as e:
            # handle PDP11 traps; store them
            print('test failed', e)
            return None

fh = open(sys.argv[1], 'w')

t = test_generator()

tests = []
for i in range(0, 4096):
    test = t.create_test()
    if test != None:
        tests.append(test)

fh.write(json.dumps(tests))
fh.close()

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
        self.mem_transactions_w = dict()

    def get_mem_transactions_dict(self):
        return self.mem_transactions_w

    def physRW(self, physaddr, value=None):
        if value == None:  # read
            return super().physRW(physaddr, value)

        self.mem_transactions_w[physaddr] = value
        super().physRW(physaddr, value)

    def physRW_N(self, physaddr, nwords, words=None):
        if words == None:
            return super().physRW_N(physaddr, nwords)

        temp_addr = physaddr
        for w in words:
            self.mem_transactions_w[temp_addr] = w
            temp_addr += 2

        super().physRW_N(physaddr, nwords, words=words)

class test_generator:
    def _invoke_bp(self, a, i):
        return True

    def _run_test(self, mem_setup, reg_setup):
        p = PDP1170_wrapper()

        for a, v in mem_setup:
            p.mmu.wordRW(a, v)

        for r, v in reg_setup:
            p.r[r] = v

        p.reset_mem_transactions_dict()

        p.run(breakpoint=self._invoke_bp)

        return p

    def create_test(self):
        out = { }

        # TODO what is the maximum size of an instruction?
        mem_kv = []
        mem_kv.append((0o1000, random.randint(0, 65536)))
        mem_kv.append((0o1002, random.randint(0, 65536)))
        mem_kv.append((0o1004, random.randint(0, 65536)))
        mem_kv.append((0o1006, random.randint(0, 65536)))

        out['memory-before'] = dict()
        for a, v in mem_kv:
            out['memory-before'][a] = v

        reg_kv = []
        for i in range(7):
            reg_kv.append((i, random.randint(0, 65536)))
        reg_kv.append((7, 0o1000))

        out['registers-before'] = dict()
        for r, v in reg_kv:
            out['registers-before'][r] = v

        try:
            p = self._run_test(mem_kv, reg_kv)

            out['registers-after'] = dict()
            for r, v in reg_kv:
                out['registers-after'][r] = v

            out['memory-after'] = dict()
            for a, v in mem_kv:
                out['memory-after'][a] = v
            mem_transactions = p.get_mem_transactions_dict()
            for a in mem_transactions:
                out['memory-after'][a] = mem_transactions[a]

            # TODO check if mem_transactions affects I/O, then return None

            return out

        except:
            print('test failed')
            return None

fh = open(sys.argv[1], 'w')

t = test_generator()

tests = []
for i in range(0, 1024):
    test = t.create_test()
    if test != None:
        tests.append(test)

fh.write(json.dumps(tests))
fh.close()

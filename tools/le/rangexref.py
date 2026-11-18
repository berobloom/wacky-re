#!/usr/bin/env python3
"""Field census of a data block: every decoded instruction whose operand holds an
address inside a range.  `xref.py <base>` finds only the whole-block transfers -
Watcom reaches a struct/array FIELD through its own absolute address
(cfg7ee1c + 0x3e is `word [0x7ee5a]`), so a census has to cover the range.

usage: rangexref.py <prog> 0xLO 0xHI [0xLO2 0xHI2 ...]
"""
import bisect
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from lelib import Image  # noqa: E402
import funcs  # noqa: E402


def main():
    prog = sys.argv[1]
    a = [int(x, 16) for x in sys.argv[2:]]
    ranges = [(a[i], a[i + 1]) for i in range(0, len(a), 2)]
    img = Image(prog)
    d = funcs.analyse(img)
    starts = sorted(d.funcs)
    hits = {}
    for ad, ins in d.insn.items():
        for m in re.finditer(r'0x[0-9a-f]+', ins.op_str):
            v = int(m.group(0), 16)
            for lo, hi in ranges:
                if lo <= v < hi:
                    hits.setdefault((lo, v), []).append((ad, ins))
    for lo, v in sorted(hits):
        for ad, ins in sorted(hits[(lo, v)]):
            f = starts[bisect.bisect_right(starts, ad) - 1]
            print('+0x%03x  0x%05x in func 0x%05x  %s %s' % (
                v - lo, ad, f, ins.mnemonic, ins.op_str))
    for lo in sorted({lo for lo, _ in hits}):
        print('range 0x%x: %d distinct addresses' % (
            lo, len({v for l2, v in hits if l2 == lo})))


if __name__ == '__main__':
    main()

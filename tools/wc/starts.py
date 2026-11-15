#!/usr/bin/env python3
"""starts.py: function start candidates in a Watcom-compiled range of an LE program's code object.

usage: starts.py PROG 0xLO 0xHI

A start is a 4-aligned address that is a direct call target, or that follows a ret/iret/jmp and the
compiler's 4-byte padding (90 / 8b c0 / 8d 40 00). Prints each with its source (call, after-ret, both).
Watcom pads functions to 4 inside a module, so this finds the functions with no register-save prologue
that a prologue scanner misses. False positives: jump-table targets and loop bodies that follow a jmp
(check that the previous instruction really ends a function).
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PADS = (b'', b'\x90', b'\x8b\xc0', b'\x8d\x40\x00')


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    prog, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
    le = os.path.join(ROOT, '.work', 'le')
    base = json.load(open(os.path.join(le, prog + '.json')))['objects'][0]['base']
    c = open(os.path.join(le, prog + '.obj1.bin'), 'rb').read()
    calls = set()
    for i in range(len(c) - 5):
        if c[i] == 0xE8:
            t = i + 5 + struct.unpack_from('<i', c, i + 1)[0] + base
            if lo <= t < hi:
                calls.add(t)
    after = set()
    for a in range(lo, hi, 4):
        o = a - base
        for pad in PADS:
            p = o - len(pad)
            if c[p:o] == pad and (c[p - 1] in (0xC3, 0xCF) or (p >= 5 and c[p - 5] == 0xE9)):
                after.add(a)
                break
    for a in sorted(calls | after):
        print('0x%06x %s' % (a, 'both' if a in calls and a in after else 'call' if a in calls else 'after-ret'))
    return 0


if __name__ == '__main__':
    sys.exit(main())

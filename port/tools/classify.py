#!/usr/bin/env python3
"""classify.py: where each name src/ww.c leaves undefined comes from in the game.

usage: port/tools/classify.py UNDEF SYMMAP SRC    (UNDEF: `nm -u` names, one per line)

Prints name, game address and layer: data (object 2, >= 0x60000), C (our own module), asm
(src/wwasm.asm, 0x36a4e-0x409ee), lib (clib and the sound library above it), unknown.
Pragma aliases (`#pragma aux X "Y"`) resolve to their target."""
import re
import sys


def load(undef, symmap, src):
    sm = {}
    for line in open(symmap):
        f = line.split()
        if len(f) >= 2 and f[1] != '-':
            sm[f[0]] = int(f[1], 16)
    alias = dict(re.findall(r'#pragma aux (\w+)\s+"(\w+)"', open(src, encoding='latin-1').read()))
    rows = []
    for u in (l.strip() for l in open(undef)):
        if not u:
            continue
        t = alias.get(u)
        cands = [t] if t else ['_' + u, u + '_', u]
        sym = next((c for c in cands if c in sm), None)
        a = sm.get(sym)
        if a is None:
            k = 'unknown'
        elif a >= 0x60000:
            k = 'data'
        elif a < 0x36a4e:
            k = 'C'
        elif a <= 0x409ee:
            k = 'asm'
        else:
            k = 'lib'
        rows.append((u, a, k, sym))
    return rows


if __name__ == '__main__':
    for u, a, k, sym in load(*sys.argv[1:4]):
        print('%-24s %s %-7s %s' % (u, '%06x' % a if a is not None else '------', k, sym or '-'))

#!/usr/bin/env python3
"""Per-target fixup census, the game's code object against the hybrid's (tools/link/build.sh).

A data address our C spells as a number (`WI(0x6e6e0 + edx)`, `v = 0x70660`) compiles to the
game's bytes but carries no fixup, so DOS/4GW, which loads DGROUP away from its link base, leaves
it pointing at the raw link address. No byte comparison sees this (check.sh, fndiff, globrefs).
The delinked part is the game's own, so a data target the game relocates and the hybrid never
does is such a literal (or a function-local static, which our build moves to its own _BSS).

usage: fixcensus.py [game.exe] [hybrid.exe]   (defaults original/WW.EXE .work/link/WWNEW.EXE)
Prints one line per target the game references and the hybrid never does; exit 1 if any.
String literals (the game's CONST group 0x60004-0x6235a) and our own data (0x94000 on) are layout.
"""
import collections
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'le'))
from leparse import LE  # noqa: E402

KNOWN = {0x88cf8: 'crchware static i', 0x88cfa: 'crcupdate static comb_val'}


def census(path):
    le = LE(open(path, 'rb').read())
    c = collections.Counter()
    code = le.objects[0]
    for k in range(code['page_count']):
        for _st, _so, tobj, toff in le.fixups(code['page_index'] + k):
            if tobj != 1:
                c[le.objects[tobj - 1]['base'] + toff] += 1
    return c


def missing(game, hybrid):
    return [(t, game[t]) for t in sorted(game)
            if 0x6235b <= t < 0x94000 and hybrid[t] == 0]


def main():
    g = census(sys.argv[1] if len(sys.argv) > 1 else 'original/WW.EXE')
    h = census(sys.argv[2] if len(sys.argv) > 2 else '.work/link/WWNEW.EXE')
    bad = 0
    for t, n in missing(g, h):
        why = KNOWN.get(t)
        print('0x%x game %d hybrid 0  %s' % (t, n, why or 'UNRELOCATED LITERAL?'))
        bad += why is None
    print('fixcensus: %d unexplained' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())

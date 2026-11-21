#!/usr/bin/env python3
"""flipsim.py: which change to 9.5b's conflict list would let conflict A be allocated before B.

usage: tools/wcc95/flipsim.py LOG A B [PASS]     (A, B: temp ids from the log, e.g. 2 and 3)

LOG is a ratrace.py trace (findings/compiler.md). Over the PASS-th pre-sort list (default 0) it
simulates SortConflicts (shellsort.py) and reports, for the two conflicts:
  - the order now;
  - A's or B's savings +-1..64 that put A first (savings are unsigned, higher first);
  - one extra conflict of savings S inserted at position k that puts A first;
  - one conflict removed that puts A first;
  - the positions A would win at, everything else unchanged.
The first conflict allocated takes the first free register when the candidates score alike, so a
flip of this order is what a register "coin flip" between A and B needs. It says what to aim for,
not which C produces it: a source change has to reach the list (trace it with vtrace.py)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shellsort import passes, shellsort  # noqa: E402


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    log, a, b = argv[0], argv[1], argv[2]
    p = list(passes(log))[int(argv[3]) if len(argv) > 3 else 0]
    pre = [(s, i) for _, s, i in p['pre']]
    ids = [i for _, i in pre]
    if a not in ids or b not in ids:
        sys.exit('ids %s, %s not both in the pass at line %s: %s' % (a, b, p['line'], ' '.join(ids)))

    def a_first(lst):
        o = [i for _, i in shellsort(lst, lambda x, y: x[0] > y[0])]
        return o.index(a) < o.index(b)

    ia, ib = ids.index(a), ids.index(b)
    print('pass at line %s, %d conflicts; id%s at %d s%d, id%s at %d s%d: %s first now' % (
        p['line'], len(pre), a, ia, pre[ia][0], b, ib, pre[ib][0], 'id' + (a if a_first(pre) else b)))
    for who, ix in ((a, ia), (b, ib)):
        ok = []
        for d in range(-64, 65):
            if d and pre[ix][0] + d >= 0:
                l = list(pre)
                l[ix] = (pre[ix][0] + d, pre[ix][1])
                if a_first(l):
                    ok.append(d)
        print('  id%s savings change that puts id%s first: %s' % (who, a, span(ok)))
    for s in sorted({x for x, _ in pre} | {1, 2, 4, 8, 16, 64}):
        ok = [k for k in range(len(pre) + 1) if a_first(pre[:k] + [(s, 'x')] + pre[k:])]
        if ok:
            print('  extra conflict s%-5d inserted at %s' % (s, span(ok)))
    rm = ['%d (id%s s%d)' % (k, i, s) for k, (s, i) in enumerate(pre) if i not in (a, b) and a_first(pre[:k] + pre[k + 1:])]
    print('  one conflict removed: %s' % (', '.join(rm) or 'none'))
    rest = pre[:ia] + pre[ia + 1:]
    ok = [k for k in range(len(pre)) if a_first(rest[:k] + [pre[ia]] + rest[k:])]
    print('  id%s moved to position: %s' % (a, span(ok)))


def span(xs):
    if not xs:
        return 'none'
    out, start = [], xs[0]
    for x, y in zip(xs, xs[1:] + [None]):
        if y != x + 1:
            out.append(str(start) if start == x else '%d..%d' % (start, x))
            start = y
    return ', '.join(out)


if __name__ == '__main__':
    main(sys.argv[1:])

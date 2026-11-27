#!/usr/bin/env python3
"""slotsim.py: 9.5b's stack-slot order, simulated from a tools/wcc95/slottrace.py log
(findings/compiler.md "Stack slots").  The chain, CONFIRMED by traces:
  Names[N_TEMP] (locals in declaration order, then the parameters reversed, then compiler temps)
  -> RoughSortTemps 0x2dfa4: AllocBefore 0x2dec2 (const temps last; names with a conflict first,
     higher rough savings first (unsigned); the rest higher id first).  Rough savings = Weight(1, blk)
     per IR operand/result reference: 1 outside loops, x20 per loop level (OW dataflo.c AddTempSave)
  -> temps created later are pushed in front
  -> AssignOtherLocals 0x3e674: TempAllocBefore 0x3e716 (smaller size first; nothing else), and the
     first temp to get memory gets the highest slot.
Both sorts are ShellSort 0x3d51f, unstable: equal keys keep whatever order the gaps leave.

usage: slotsim.py LOG                    every PASS: simulated size sort = traced POST order
       slotsim.py LOG FUNCNO sym=NAME..  one function (FUNCNO = its PASS number; use a probe cut after
                                         the target, so the target is the last PASS): check the chain,
                                         print the simulated top-down order of the named temps
       ... --want NAME,NAME,..           also search what gives that top-down order: declaration orders
                                         of the wanted names, rough savings -2..+2 each (5^n runs: keep n <= 7), any subset as int
"""
import itertools
import os
import re
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shellsort import shellsort  # noqa: E402

R = re.compile(r'\s+(RPRE|RPOST|PRE |POST) n([0-9a-f]+) sz(\d+) us([0-9a-f]+) fl([0-9a-f]{2})([0-9a-f]{2}) '
               r'id(\d+) sym([0-9a-f]+) off(\d+) blk\S+ loc\S+(?: s(\d+))?')


def rough(a, b):
    if a['f2d'] & 1 and not b['f2d'] & 1: return False
    if not a['f2d'] & 1 and b['f2d'] & 1: return True
    if a['sav'] is None: return False if b['sav'] is not None else a['id'] > b['id']
    if b['sav'] is None: return True
    return a['sav'] > b['sav']


def slot(a, b):
    fa, fb = a['f2d'] & 2, b['f2d'] & 2
    if fa and not fb: return True
    if fb and not fa: return False
    if a['sz'] < b['sz']: return True
    if a['sz'] != b['sz']: return False
    return a['id'] == b['id'] and a['off'] > b['off']


def blocks(log):
    """per function: a list of sorts, each {'kind', 'pre', 'post'}; functions end at END n"""
    fns = {}; fn = 1; cur = None
    for l in open(log):
        if l.startswith('END'):
            fn = int(l.split()[1]) + 1; cur = None; continue
        if 'ROUGHSORT' in l or 'IDSORT' in l or l.startswith('PASS'):
            cur = {'kind': l.split()[0], 'pre': [], 'post': []}; fns.setdefault(fn, []).append(cur); continue
        m = R.match(l)
        if m and cur is not None:
            t = dict(n=m.group(2), sz=int(m.group(3)), us=int(m.group(4), 16), f2d=int(m.group(5), 16),
                     f2c=int(m.group(6), 16), id=int(m.group(7)), sym=m.group(8), off=int(m.group(9)),
                     sav=None if m.group(10) is None else int(m.group(10)))
            cur['pre' if m.group(1).strip() in ('RPRE', 'PRE') else 'post'].append(t)
    return fns


def main():
    a = sys.argv[1:]
    want = None
    if '--want' in a:
        k = a.index('--want'); want = a[k + 1].split(','); del a[k:k + 2]
    fns = blocks(a[0])
    if len(a) == 1:
        bad = n = 0
        for fn, bs in fns.items():
            for b in bs:
                if b['kind'] != 'PASS': continue
                n += 1
                if [t['n'] for t in shellsort(b['pre'], slot)] != [t['n'] for t in b['post']]:
                    bad += 1; print('function %d: size sort DIFFERS (a list longer than the 300 logged?)' % fn)
        print('%d passes, %d differ' % (n, bad)); return
    fn = int(a[1]); names = dict((x.split('=')[0].replace('sym', ''), x.split('=')[1]) for x in a[2:])
    bs = fns[fn]
    rs = [b for b in bs if b['kind'] == 'ROUGHSORT']; ps = [b for b in bs if b['kind'] == 'PASS']
    if not rs or not ps:
        print('function %d: no ROUGHSORT or PASS logged' % fn); return
    rpre, pre = rs[0]['pre'], ps[-1]['pre']
    rpost = shellsort(rpre, rough)
    ok_r = [t['n'] for t in rpost] == [t['n'] for t in rs[0]['post']]
    k = len(pre) - len(rpost)
    ok_c = [t['n'] for t in pre[k:]] == [t['n'] for t in rpost]
    new = pre[:k]
    print('rough sort simulated = traced: %s; slot list = %d new temps + rough output: %s' % (ok_r, k, ok_c))
    print('new temps (sizes):', ''.join(str(t['sz']) if t['sz'] < 10 else '+' for t in new))
    print('declaration order:', ' '.join('%s(s%s)' % (names[t['sym']], t['sav']) for t in rpre if t['sym'] in names
                                         and not t['f2c'] & 2 and t['sz'] <= 4))
    mem = {t['sym'] for t in pre if t['us'] & 0x80 and not t['f2c'] & 2}     # got stack memory
    W = [v for s, v in names.items() if s in mem]

    def order(rp, nw=new):
        out = []
        for t in shellsort(nw + shellsort(rp, rough), slot):
            v = names.get(t['sym'])
            if v in W and v not in out and not t['f2c'] & 2 and (want is None or v in want): out.append(v)
        return out
    print('simulated slots, top down:', ' '.join(order(rpre)))
    if not want: return
    print('want:                      ', ' '.join(want))
    pos = [i for i, t in enumerate(rpre) if names.get(t['sym']) in want and not t['f2c'] & 2]
    hits = 0
    for perm in itertools.permutations(pos):
        rp = list(rpre)
        for x, y in zip(pos, perm): rp[x] = rpre[y]
        if order(rp) == want:
            hits += 1
            if hits <= 5: print('  declaration order', ' '.join(names[rp[i]['sym']] for i in pos))
    print('declaration orders: %d hits' % hits)
    hits = 0
    for d in itertools.product(range(-2, 3), repeat=len(pos)):
        rp = [dict(t) for t in rpre]
        for i, dv in zip(pos, d):
            if rp[i]['sav'] is not None: rp[i]['sav'] += dv
        if order(rp) == want:
            hits += 1
            if hits <= 5: print('  savings', ' '.join('%s%+d' % (names[rpre[i]['sym']], dv) for i, dv in zip(pos, d) if dv))
    print('savings -2..+2: %d hits' % hits)
    hits = 0
    for r in range(1, len(pos) + 1):
        for S in itertools.combinations(pos, r):
            rp = [dict(t) for t in rpre]
            for i in S: rp[i]['sz'] = 4
            if order(rp) == want:
                hits += 1; print('  as int:', ' '.join(names[rpre[i]['sym']] for i in S))
    print('int subsets: %d hits' % hits)


main()

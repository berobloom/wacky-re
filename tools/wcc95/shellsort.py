#!/usr/bin/env python3
"""shellsort.py: 9.5b's SortList over the conflict list (findings/compiler.md): ShellSort 0x3D51F
with ConfBefore 0x44AAB (savings, unsigned, higher first). Checks a ratrace log: the simulated
order of each pass against the logged allocation order.

usage: tools/wcc95/shellsort.py LOG [FUNC]
"""
import re, sys

def shellsort(a, before):
    a = list(a); n = len(a); gap = n; adjust = 1
    if n < 2:
        return a
    while True:
        adjust = 0 if adjust else 1
        gap = gap // 2 + adjust
        while True:
            swap = False
            for i in range(n - gap):
                if before(a[i + gap], a[i]):
                    a[i], a[i + gap] = a[i + gap], a[i]; swap = True
            if not swap:
                break
        if gap == 1:
            return a

def passes(log):
    cur = None
    for l in open(log):
        m = re.match(r'PRESORT (\S+) line (\d+)', l)
        if m:
            cur = {'fn': m.group(1), 'line': m.group(2), 'pre': [], 'order': []}; yield cur; continue
        m = re.match(r'\s+c([0-9a-f]+) s(\d+) id(\d+)', l)
        if m and cur is not None and not cur['order']:
            cur['pre'].append((m.group(1), int(m.group(2)), m.group(3))); continue
        m = re.match(r'ASSIGN c([0-9a-f]+)', l)
        if m and cur is not None:
            cur['order'].append(m.group(1))

if __name__ == '__main__':
    for p in list(passes(sys.argv[1])):
        if len(sys.argv) > 2 and p['fn'] != sys.argv[2]:
            continue
        sim = [c for c, _, _ in shellsort(p['pre'], lambda x, y: x[1] > y[1])]
        logged = p['order']
        ok = [c for c in sim if c in logged] == logged
        print('%-16s line %-6s %3d conflicts: %s' % (p['fn'], p['line'], len(sim), 'simulation = trace' if ok else 'DIFFERS'))

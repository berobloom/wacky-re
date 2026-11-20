#!/usr/bin/env python3
"""vtrace.py: trace source variants of a probe function and print chosen conflicts' savings and
registers, plus the fndiff rows.  usage: vtrace.py PROBE.C FUNC 0xADDR VARIANTS.py symHANDLE=NAME...
(DREA_CONF=.work/wcc/wcc.conf, DOSBox-X running).  VARIANTS.py as for vbatch.py.
Handles of locals depend on the probe's declarations: give them as the control trace shows them."""
import os, re, subprocess, sys, shutil
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
probe, func, addr, vfile = sys.argv[1:5]
want = dict(x.split('=') for x in sys.argv[5:])
ns = {}; exec(open(vfile).read(), ns)
src = open(probe).read()
PY = os.path.join(ROOT, '.work', 'venv', 'bin', 'python')
for label, reps in [('control', [])] + ns['VARIANTS']:
    s = src
    for old, new in reps:
        assert s.count(old) == 1, (label, old)
        s = s.replace(old, new)
    open(os.path.join(ROOT, '.work/wcc/probe/V.C'), 'w').write(s)
    subprocess.run([PY, 'tools/wcc95/ratrace.py', 'V.C', '.work/wcc/v.txt', '%s:1-99999' % func, '90'],
                   cwd=ROOT, capture_output=True)
    got = {}; pre = []; npre = None
    for l in open(os.path.join(ROOT, '.work/wcc/v.txt')):
        m = re.match(r'PRESORT \S+ line \d+ n (\d+)', l)
        if m and npre is None:
            npre = int(m.group(1)); continue
        m = re.match(r'\s+c[0-9a-f]+ s\d+ id\d+ sym([0-9a-f]+)', l)
        if m and npre is not None and len(pre) < npre:
            pre.append(want.get('sym' + m.group(1), '.')); continue
        m = re.match(r'ASSIGN c([0-9a-f]+) s(\d+) \S+ id(\d+) sym([0-9a-f]+)', l)
        if m and 'sym' + m.group(4) in want and want['sym' + m.group(4)] not in got:
            got[want['sym' + m.group(4)]] = [m.group(2), m.group(1), '?']
        m = re.match(r'GIVEN  c([0-9a-f]+) (\S+)', l)
        if m:
            for v in got.values():
                if v[1] == m.group(1) and v[2] == '?': v[2] = m.group(2)
    r = subprocess.run([PY, 'tools/wc/fndiff.py', 'ww', '.work/wcc/probe/V.OBJ', func, addr], cwd=ROOT,
                       capture_output=True, text=True).stdout.strip().splitlines()
    rows = re.search(r'(\d+) instruction diffs', r[-1]).group(1) if r and 'diffs' in r[-1] else '?'
    pos = ' '.join('%s@%d' % (k, pre.index(k)) for k in sorted(set(want.values())) if k in pre)
    print('%-36s rows %-3s nodes %-3s %-14s %s' % (label, rows, npre, pos, '  '.join('%s s%s %s' % (k, v[0], v[2]) for k, v in sorted(got.items()))))

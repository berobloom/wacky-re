#!/usr/bin/env python3
"""vbatch.py: compile source variants of one function in a probe file and score each against the
game (tools/wc/fndiff.py rows).  usage: vbatch.py PROBE.C FUNC 0xADDR VARIANTS.py
PROBE.C may be src/ww.c itself (35 s a variant). VARIANTS.py defines VARIANTS = [(label, [(old, new), ...]), ...]; each pair is replaced once in PROBE.C.
"""
import os, re, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
probe, func, addr, vfile = sys.argv[1:5]
ns = {}; exec(open(vfile).read(), ns)
src = open(probe).read()
d = os.path.join(ROOT, '.work', 'vbatch')      # one scratch dir, reused
os.makedirs(d, exist_ok=True)
for label, reps in [('control', [])] + ns['VARIANTS']:
    s = src; ok = True
    for old, new in reps:
        if s.count(old) != 1:
            print('%-40s pattern not found once: %r' % (label, old[:40])); ok = False; break
        s = s.replace(old, new)
    if not ok:
        continue
    for f in ('V.OBJ', 'WCC.OUT'):
        if os.path.exists(os.path.join(d, f)): os.remove(os.path.join(d, f))
    open(os.path.join(d, 'V.C'), 'w').write(s)
    out = subprocess.run(['tools/wc/wcc.sh', '.work/toolchains/inst/WC95B', d, 'V.C', '-s', '-3r', '-oneatx'],
                         cwd=ROOT, capture_output=True, text=True).stdout
    if not os.path.exists(os.path.join(d, 'V.OBJ')):
        print('%-40s compile failed: %s' % (label, out.strip().splitlines()[-1:])); continue
    r = subprocess.run(['.work/venv/bin/python', 'tools/wc/fndiff.py', 'ww', os.path.join(d, 'V.OBJ'), func, addr],
                       cwd=ROOT, capture_output=True, text=True).stdout.strip().splitlines()
    print('%-40s %s' % (label, r[-1] if r else '?'))

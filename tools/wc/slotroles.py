#!/usr/bin/env python3
"""slotroles.py PROBE.C FUNC 0xADDR VARIANTS.py: like tools/wcc95/vbatch.py, but also prints which
slot each ROLE got. VARIANTS.py: VARIANTS as vbatch, ROLES = [(name, regex with the slot as (S))]."""
import os, re, subprocess, sys
ROOT = os.getcwd()
probe, func, addr, vfile = sys.argv[1:5]
ns = {}; exec(open(vfile).read(), ns)
src = open(probe).read(); d = '.work/vbatch'
def roles(txt):
    out = []
    for name, rx in ns['ROLES']:
        rx = rx.replace('(S)', r'\[esp(?: \+ (0x[0-9a-f]+|\d+))?\]')
        m = re.search(rx, txt, re.M)
        out.append('%s=%s' % (name, ('%x' % int(m.group(1) or '0', 0)) if m else '?'))
    return ' '.join(out)
first = True
for label, reps in [('control', [])] + ns['VARIANTS']:
    s = src; ok = True
    for old, new in reps:
        if s.count(old) != 1: print('%-28s pattern not once: %r' % (label, old[:40])); ok = False; break
        s = s.replace(old, new)
    if not ok: continue
    for f in ('V.OBJ',):
        if os.path.exists(d + '/' + f): os.remove(d + '/' + f)
    open(d + '/V.C', 'w').write(s)
    subprocess.run(['tools/wc/wcc.sh', '.work/toolchains/inst/WC95B', d, 'V.C', '-s', '-3r', '-oneatx'], capture_output=True)
    if not os.path.exists(d + '/V.OBJ'): print('%-28s compile failed' % label); continue
    r = subprocess.run(['.work/venv/bin/python', 'tools/wc/fndiff.py', 'ww', d + '/V.OBJ', func, addr], capture_output=True, text=True).stdout.strip().splitlines()
    sm = subprocess.run(['.work/venv/bin/python', 'tools/wc/stackmap.py', d + '/V.OBJ', 'ww', func, '-v'], capture_output=True, text=True).stdout
    o, g = sm.split('\ngame ')
    if first: print('%-28s      %s' % ('GAME', roles(g))); first = False
    print('%-28s %3s  %s' % (label, re.search(r'(\d+) instruction diffs', r[-1]).group(1), roles(o)), flush=True)

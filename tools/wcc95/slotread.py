#!/usr/bin/env python3
"""slotread.py: one function of a tools/wcc95/slottrace.py log: every stack location 9.5b handed out
(SetTempLocation, in order), the caller, the temp's size, id, FE symbol and its position in
Names[N_TEMP] before and after TempAllocBefore's sort (`-` = not in the list, `*` = before the sort).

usage: tools/wcc95/slotread.py LOG FUNCNO [symHANDLE=NAME ...]
FUNCNO counts the functions in compile order (the PASS numbers; static functions count too).
Callers (image addresses): 0x3e876.. AllocNewLocal's StackEntry path, 0x3e8a0 NewLocation; the
caller of AllocNewLocal is printed after `via`.
"""
import re, sys
log, want = sys.argv[1], int(sys.argv[2])
names = dict(x.split('=') for x in sys.argv[3:])
fn = 1; inpass = False; pre = []; post = []; ev = []; via = None
for l in open(log):
    if l.startswith('END'):
        fn = int(l.split()[1]) + 1; inpass = False; continue
    if l.startswith('PASS'):
        inpass = True; continue
    if fn != want: continue
    m = re.match(r'\s+ALLOC (n[0-9a-f]+) .* from (-?[0-9a-f]+)', l)
    if m: via = m.group(2); continue
    m = re.match(r'\s+SETLOC (n[0-9a-f]+) (.*) size (\d+) from (-?[0-9a-f]+)', l)
    if m:
        ev.append((m.group(1), m.group(2), m.group(3), m.group(4), via, inpass)); via = None; continue
    m = re.match(r'\s+(PRE |POST) (n[0-9a-f]+)', l)
    if m: (pre if m.group(1) == 'PRE ' else post).append(m.group(2))
pi = {n: k for k, n in enumerate(pre)}
qi = {n: k for k, n in enumerate(post)}
print('function %d: %d temps in the list, %d locations handed out' % (want, len(pre), len(ev)))
off = 0
for n, rest, size, frm, via, inpass in ev:
    off += int(size)
    sym = re.search(r'sym([0-9a-f]+)', rest).group(1)
    f = rest.split()
    print('  -%-3d %-8s %s pre %3s post %3s  %-5s %-6s sym%-5s from %s via %s  %s' % (
        off, n, ' ' if inpass else '*', pi.get(n, '-'), qi.get(n, '-'), f[0], f[3], sym, frm, via,
        names.get('sym' + sym, '')))

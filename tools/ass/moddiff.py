#!/usr/bin/env python3
"""moddiff.py: where an ASS module (src/ass) first differs from the game, as disassembly side by side.

usage: tools/ass/moddiff.py MODULE [N] [BASE]   (after tools/ass/build.sh; N = instructions to show, default 24;
                                                  BASE = the game address of the segment's start, when the
                                                  automatic placement is off)

Places .work/ass/19941101/MODULE.OBJ's _TEXT by its first public that objmatch.py MATCHes (or, failing
that, its first public with an address in analysis/ww_names.txt), finds the first unmasked byte that
differs from WW.EXE, and prints both sides from the start of the enclosing function, ours left (offsets
in the segment) and the game's right (addresses), names substituted from the names file."""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
import capstone  # noqa: E402
import libmatch as L  # noqa: E402
import objmatch  # noqa: E402
from lelib import Image  # noqa: E402


def main(argv):
    mod = argv[0].upper()
    n = int(argv[1]) if len(argv) > 1 else 24
    obj = os.path.join(ROOT, '.work', 'ass', '19941101', mod + '.OBJ')
    recs = [r for _, r in L.modules(open(obj, 'rb').read())][0]
    buf, mask = L.code_of(recs)['_TEXT']
    pubs = sorted((o, nm) for s, o, nm in objmatch.publics(recs) if s == '_TEXT')
    names = {int(a, 16): nm for a, nm in re.findall(r'^(0x[0-9a-f]+) (\S+)', open(os.path.join(ROOT, 'analysis', 'ww_names.txt')).read(), re.M)}
    byname = {nm: a for a, nm in names.items()}
    out = subprocess.run([sys.executable, 'tools/wc/objmatch.py', 'ww', obj], cwd=ROOT, capture_output=True, text=True).stdout
    base = int(argv[2], 16) if len(argv) > 2 else None
    for o, nm in pubs if base is None else []:
        m = re.search(r'^%s\s+\d+ bytes\s+MATCH at 0x([0-9a-f]+)' % re.escape(nm), out, re.M)
        if m:
            base = int(m.group(1), 16) - o
            break
    if base is None:
        for o, nm in pubs:
            if nm.strip('_') in byname and base is None:
                base = byname[nm.strip('_')] - o
                break
    if base is None:
        sys.exit('no public of %s has an address' % mod)
    img = Image('ww')
    g = img.code[base - img.code_base:base - img.code_base + len(buf)]
    d = [i for i in range(len(buf)) if not mask[i] and buf[i] != g[i]]
    print('%s: _TEXT %d bytes at 0x%05X, %d bytes differ' % (mod, len(buf), base, len(d)))
    if not d:
        return 0
    start = max([o for o, _ in pubs if o <= d[0]] + [0])
    fn = [nm for o, nm in pubs if o == start]
    # statics have no public: start at the nearest instruction boundary 48 bytes back instead
    if d[0] - start > 4000:
        start = d[0] - 48
    print('first difference at +0x%x (game 0x%05X), from %s' % (d[0], base + d[0], fn[0] if fn and start in [o for o, _ in pubs] else '+0x%x' % start))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def sub(ops):
        for h in re.findall(r'0x[0-9a-f]{5}', ops):
            if int(h, 16) in names:
                ops = ops.replace(h, names[int(h, 16)])
        return ops
    lo = d[0] - 48              # decode from the function start (aligned), print from shortly before the difference
    a = ['%4x  %s %s' % (i.address, i.mnemonic, i.op_str) for i in md.disasm(bytes(buf[start:]), start) if i.address >= lo][:n]
    b = ['%05x  %s %s' % (i.address, i.mnemonic, sub(i.op_str)) for i in md.disasm(bytes(g[start:]), base + start)
         if i.address - base >= lo][:n]
    for x, y in zip(a + [''] * (n - len(a)), b + [''] * (n - len(b))):
        print('%-44s %s' % (x[:44], y))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

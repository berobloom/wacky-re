#!/usr/bin/env python3
"""symmap.py: the game address of every external name our object uses.

usage: tools/link/symmap.py [OBJ] [--out FILE]      (default .work/src/WW.OBJ, .work/link/symmap.txt)

Three sources, cross-checked:
  observed  - the game's bytes at one of our fixup sites, taken only where the 8 bytes before the
              site agree with ours (fixup bytes masked), so the site is the same instruction;
  names     - analysis/ww_names.txt;
  hex       - a letter prefix, five hex digits and at most one more letter (v88cd2, fn_32e60, f37bf0,
              sp7b324b = a byte view of 0x7b324) is its address.
The names file and the hex in a name are authoritative, observed only confirms them (a single
near-miss site can be a different instruction); a name no fixup uses (_cstart_, __argc: pulled in
by the compiler, never addressed) is 'unreferenced'. Prints every disagreement and every name with no address; writes `name addr source` lines.
Exit 1 when a name has no address or the sources disagree."""
import os
import re
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
import libmatch as L                    # noqa: E402
import objmatch as M                    # noqa: E402

CBASE = 0x10000


def observed(recs, code, names):
    """{external name: Counter(target address - addend)} from sites whose context matches"""
    segs = L.code_of(recs)
    pubs = sorted((off, seg, n) for seg, off, n in M.publics(recs) if seg in segs)
    out = defaultdict(Counter)
    for seg, at, loc, selfrel, kind, nm, disp in L.fixups_of(recs):
        if loc != 9 or kind != 'ext':
            continue
        buf, mask = segs[seg]
        if not selfrel:
            disp += struct.unpack_from('<i', bytes(buf), at)[0]
        fn = None
        for off, s2, n in pubs:
            if s2 == seg and off <= at:
                fn = (off, n)
        if not fn or M.plain(fn[1]) not in names:
            continue
        game_at = names[M.plain(fn[1])] + at - fn[0]
        g = game_at - CBASE
        if g < 8 or g + 4 > len(code):
            continue
        if any(not mask[at - k] and buf[at - k] != code[g - k] for k in range(1, 9)):
            continue
        v = struct.unpack_from('<I', code, g)[0]
        tgt = (game_at + 4 + v) & 0xFFFFFFFF if selfrel else v
        out[nm][tgt - disp] += 1
    return out


def externs(recs):
    out = []
    for t, b in recs:
        if t == 0x8C:
            p = 0
            while p < len(b):
                n = b[p]
                out.append(b[p + 1:p + 1 + n].decode('latin1'))
                p += 1 + n
                _, p = L.index(b, p)
    return out


def main(argv):
    obj = '.work/src/WW.OBJ'
    out = '.work/link/symmap.txt'
    args = list(argv)
    if '--out' in args:
        i = args.index('--out')
        out = args[i + 1]
        del args[i:i + 2]
    if args:
        obj = args[0]
    os.chdir(ROOT)
    (_, recs), = list(L.modules(open(obj, 'rb').read()))
    code = open('.work/le/ww.obj1.bin', 'rb').read()
    names = M.names('ww')
    obs = observed(recs, code, names)
    used = {nm for _, _, _, _, kind, nm, _ in L.fixups_of(recs) if kind == 'ext'}
    rows, bad, missing = [], [], []
    for e in externs(recs):
        p = M.plain(e)
        cands = {}
        if e in obs:
            a, n = obs[e].most_common(1)[0]
            cands['observed'] = a
        if p in names:
            cands['names'] = names[p]
        # five hex digits starting 1-9 (every address is 0x1xxxx..0x9xxxx), at most one letter after:
        # sp7b324b = a 'b'yte view of 0x7b324, not 0xb324b; music88bcc is 0x88bcc, readdat no address
        m = re.fullmatch(r'[a-z_0-9]*?([1-9][0-9a-f]{4})[a-z]?', p)
        if m:
            cands['hex'] = int(m.group(1), 16)
        if not cands:
            if e not in used:
                rows.append((e, None, 'unreferenced'))
            else:
                missing.append(e)
            continue
        src = next(s for s in ('names', 'hex', 'observed') if s in cands)
        if 'observed' in cands and cands['observed'] != cands[src]:
            bad.append('%s %s' % (e, ' '.join('%s 0x%x' % kv for kv in cands.items())))
        if 'names' in cands and 'hex' in cands and cands['names'] != cands['hex']:
            bad.append('%s %s' % (e, ' '.join('%s 0x%x' % kv for kv in cands.items())))
        rows.append((e, cands[src], src))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w') as f:
        for e, a, s in rows:
            f.write('%s %s %s\n' % (e, '-' if a is None else '0x%06x' % a, s))
    print('%d externals: %d mapped (%s), %d without an address, %d disagreements'
          % (len(rows) + len(missing), len(rows),
             ', '.join('%s %d' % kv for kv in Counter(s for _, _, s in rows).items()),
             len(missing), len(bad)))
    for b in bad:
        print('  DISAGREE ' + b)
    for m in missing:
        print('  NO ADDRESS ' + m)
    return 1 if bad or missing else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

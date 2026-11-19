#!/usr/bin/env python3
"""fixsites.py: in the functions that MATCH, does the operand the linker fills in point where the
game's operand points?

usage: fixsites.py PROG FILE.OBJ

check.sh masks every byte a fixup fills, so a function can MATCH while reading another global,
calling another function or passing another string literal: that is the class of the bugs the byte
count hid (fn_1832c's card count, fn_16eac's +0x20, fn_173c4's table at 0x7b230). globrefs and
nmrefs ask this of the near misses; this asks it of the MATCHes, where the mirror address is exact
because the unmasked bytes are equal.

For each public in _TEXT whose bytes equal the game's at its analysis/PROG_names.txt address (a
MATCH), every fixup site inside it is compared with the game's dword at the mirrored address:

  ext            ours = its address in .work/link/symmap.txt + the fixup disp + the addend in our
                 bytes (a global, a library entry, another module's function);
  seg _TEXT      our bytes hold a _TEXT offset. If a named public contains it and that function
                 MATCHes, ours = its game address + the intra-function offset. Inside a near miss
                 the blocks are laid out differently, so the offset decides nothing: the site is
                 collected as a switch-table entry and only the ARM ORDER of the table is compared
                 (sort both sides' entries by target and compare the permutation). Below the first
                 public WCC keeps string literals in _TEXT, so a target outside any public has its
                 bytes compared instead, as a literal.
  seg CONST/...  our bytes hold an offset in that segment (our own literal or const table); all of
                 them must agree on one link base - one outlier means one site points at another
                 element. Their bytes are not compared: a `.OBJ` data segment is a list of
                 offset/data records (CONST here has 51, and libmatch concatenates them raw), so
                 what sits at an offset in the file is not what sits there after the link.

A switch table whose targets lie in a function that does not MATCH is not decided here: a near
miss's blocks are laid out differently, so its intra-function offsets mean nothing. Such entries are
printed as `undecided` (their arms are checked against the disassembly by hand - kart_frame's table
0x29068 was, iter 62).

Exit 0 prints the summary; a disagreeing site is printed and makes the exit 1.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import libmatch as L                               # noqa: E402
import objmatch as M                               # noqa: E402
sys.path.insert(0, os.path.join(L.ROOT, 'tools', 'le'))
from lelib import Image                            # noqa: E402


def main():
    prog, obj = sys.argv[1], sys.argv[2]
    img = Image(prog)
    cbase = img.code_base
    (_, recs), = list(L.modules(open(obj, 'rb').read()))
    segs = L.code_of(recs)
    buf, mask = segs['_TEXT']
    known = M.names(prog)
    syms = {}
    for line in open(os.path.join(L.ROOT, '.work', 'link', 'symmap.txt')):
        f = line.split()
        if len(f) >= 2 and f[1].startswith('0x'):
            syms[f[0]] = int(f[1], 16)
    pubs = sorted(p for p in M.publics(recs) if p[0] in segs)
    rng = {}
    for i, (seg, off, name) in enumerate(pubs):
        if seg != '_TEXT':
            continue
        end = next((q[1] for q in pubs[i + 1:] if q[0] == seg and q[1] > off), len(buf))
        g = known.get(M.plain(name))
        b, m = buf[off:end], mask[off:end]
        ok = (g and g >= cbase and g - cbase + len(b) <= len(img.code)
              and not any(not m[k] and img.code[g - cbase + k] != b[k] for k in range(len(b))))
        rng[name] = (off, end, bool(ok))
    funcs = sorted((o, n) for n, (o, e, k) in rng.items())
    enc = lambda o: next((x for x in reversed(funcs) if x[0] <= o), None)
    nfun = sum(1 for v in rng.values() if v[2])
    counts, bad, tables, segbase = {}, [], {}, {}
    for seg, at, loc, selfrel, kind, nm, disp in L.fixups_of(recs):
        if seg != '_TEXT':
            continue
        fn = enc(at)
        if not fn or not rng[fn[1]][2]:
            continue
        off, end, _ = rng[fn[1]]
        ga = known[M.plain(fn[1])] + at - off
        gv = struct.unpack_from('<I', img.code, ga - cbase)[0]
        gtgt = (ga + 4 + gv) & 0xFFFFFFFF if selfrel else gv
        add = 0 if selfrel else struct.unpack_from('<i', bytes(buf), at)[0]
        counts[kind if kind != 'seg' else 'seg ' + nm] = counts.get(kind if kind != 'seg' else 'seg ' + nm, 0) + 1
        if kind == 'ext':
            base = syms.get(nm, syms.get('_' + M.plain(nm)))
            if base is None:
                bad.append('%s+0x%x @game 0x%x: no symmap row for %s' % (fn[1], at - off, ga, nm))
                continue
            if base + disp + add != gtgt:
                bad.append('%s+0x%x @game 0x%x: %s -> ours 0x%x, game 0x%x'
                           % (fn[1], at - off, ga, nm, base + disp + add, gtgt))
        elif kind == 'seg' and nm == '_TEXT':
            t = enc(add)
            if t and rng.get(t[1], (0, 0, False))[2]:
                ours = known[M.plain(t[1])] + add - t[0]
                if ours != gtgt:
                    bad.append('%s+0x%x @game 0x%x: _TEXT 0x%x (%s) -> ours 0x%x, game 0x%x'
                               % (fn[1], at - off, ga, add, t[1], ours, gtgt))
            elif t:
                tables.setdefault((fn[1], t[1]), []).append((add, gtgt))
            else:                       # below the first public: a literal WCC put in _TEXT
                ours, game = buf[add:add + 200].split(b'\0')[0], (img.read(gtgt, 200) or b'').split(b'\0')[0]
                if ours != game:
                    bad.append('%s+0x%x @game 0x%x: _TEXT literal 0x%x %r != game %r'
                               % (fn[1], at - off, ga, add, ours[:40], game[:40]))
        else:
            segbase.setdefault(nm, []).append(gtgt - disp - add)
    for nm, bases in sorted(segbase.items()):
        win = max(set(bases), key=bases.count)
        out = [b for b in bases if b != win]
        print('segment %-7s %d sites, base 0x%x (%d agree)' % (nm, len(bases), win, len(bases) - len(out)))
        if out:
            bad.append('%d %s sites do not fit base 0x%x' % (len(out), nm, win))
    for (site, tgt), es in sorted(tables.items()):
        print('undecided: switch table in %s, %d entries -> arms of the near miss %s, e.g. '
              'our _TEXT+0x%x vs game 0x%06x' % (site, len(es), tgt, es[0][0], es[0][1]))
    n = sum(counts.values())
    print('%d sites in %d matched functions: %s | %d disagree'
          % (n, nfun, ', '.join('%s %d' % kv for kv in sorted(counts.items())), len(bad)))
    for b in bad:
        print('  DISAGREE ' + b)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())

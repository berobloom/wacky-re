#!/usr/bin/env python3
"""objmatch.py: compare the functions of a compiled OMF .OBJ with an LE program, function by function.

usage: objmatch.py PROG FILE.OBJ [0xADDR ...]

Each public code symbol's bytes (up to the next public at a higher offset) are searched in PROG's code object with the bytes that
fixups fill masked. With addresses (one per public, in offset order; 0 skips one) each function is compared at
its address: MATCH, or the first differing byte with both sides shown, so a near miss reads like a diff.
Otherwise it is searched for (needs a run of 4 bytes no fixup touches): FOUND at the address(es), or not found.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import libmatch as L  # noqa: E402


def publics(recs):
    lnames, segs, out = [None], [None], []
    for t, b in recs:
        if t == 0x96:
            p = 0
            while p < len(b):
                n = b[p]
                lnames.append(b[p + 1:p + 1 + n].decode('latin1'))
                p += 1 + n
        elif t in (0x98, 0x99):
            p0 = 1 + (3 if b[0] >> 5 == 0 else 0)
            for lsize in ((4, 2) if t == 0x99 else (2, 4)):
                ln, p = L.index(b, p0 + lsize)
                cl, p = L.index(b, p)
                if 0 < ln < len(lnames) and 0 < cl < len(lnames) and p + 1 == len(b):
                    break
            segs.append(lnames[ln])
        elif t in (0x90, 0x91):
            g, p = L.index(b, 0)
            s, p = L.index(b, p)
            if g == 0 and s == 0:
                p += 2
            while p < len(b):
                n = b[p]
                name = b[p + 1:p + 1 + n].decode('latin1')
                p += 1 + n
                off = struct.unpack_from('<I' if t == 0x91 else '<H', b, p)[0]
                p += 4 if t == 0x91 else 2
                _, p = L.index(b, p)
                out.append((segs[s], off, name))
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    prog, obj = sys.argv[1], sys.argv[2]
    want = [int(a, 16) for a in sys.argv[3:]]
    le = os.path.join(L.ROOT, '.work', 'le')
    meta = json.load(open(os.path.join(le, prog + '.json')))
    code = open(os.path.join(le, prog + '.obj1.bin'), 'rb').read()
    cbase = meta['objects'][0]['base']
    (_, recs), = list(L.modules(open(obj, 'rb').read()))
    segs = L.code_of(recs)
    pubs = sorted(p for p in publics(recs) if p[0] in segs)
    fi = 0
    for i, (seg, off, name) in enumerate(pubs):
        buf, mask = segs[seg]
        # the next public at a higher offset (the compiler gives functions with identical code one body)
        end = next((q[1] for q in pubs[i + 1:] if q[0] == seg and q[1] > off), len(buf))
        b, m = buf[off:end], mask[off:end]
        a = want[fi] - cbase if fi < len(want) and want[fi] else None
        d = None if a is None else next((k for k in range(len(b)) if not m[k] and code[a + k] != b[k]), None)
        hits = L.find(code, b, m)
        if a is not None and d is None:
            print('%-20s %3d bytes  MATCH at 0x%06x (fixup bytes not compared)' % (name, len(b), want[fi]))
        elif hits:
            print('%-20s %3d bytes  FOUND at 0x%06x%s' % (name, len(b), hits[0] + cbase,
                                                        '' if len(hits) == 1 else ' (+%d more)' % (len(hits) - 1)))
        elif a is None:
            print('%-20s %3d bytes  not found%s' % (name, len(b), '' if b and len(b) > 8 else ' (too short to search)'))
        else:
            print('%-20s %3d bytes  differs from 0x%06x at +%d:\n      ours  %s\n      game  %s' % (
                name, len(b), want[fi], d,
                ' '.join('..' if m[k] else '%02x' % b[k] for k in range(max(0, d - 4), min(len(b), d + 12))),
                ' '.join('%02x' % code[a + k] for k in range(max(0, d - 4), min(len(b), d + 12)))))
        fi += 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

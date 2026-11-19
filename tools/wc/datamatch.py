#!/usr/bin/env python3
"""datamatch.py: compare the initialized data of a compiled .OBJ with the game's image, byte for byte.

usage: datamatch.py PROG FILE.OBJ [-v]

check.sh compares functions (objmatch.py) and the addresses their fixups hold, but never the bytes an
initialized global, a table or a string literal holds. Those are behaviour too: menu wording, a table
entry, a default value - the class of "loaded 4 cards where the game loads 3", invisible to row counts.

How it works, and the two traps it handles:

* The base. A Watcom data group is one contiguous block and objmatch's fixup census already agrees on
  one base per segment (CONST 0x60004, from 327 references), so our group's offset N sits at the game's
  address base+N. The base is derived here, not typed: every unmasked run of MINRUN bytes is searched in
  the image and the base most anchors agree on wins; the vote is printed (a census, never a literal).
  Every occurrence has to be counted - taking only the first hit of each anchor makes the shifted
  candidates win (it put this group 4 bytes off and 8225 of 9046 bytes "differed").
* A differing byte is only behaviour if something reads it. The game's linker shares pooled literals
  (findings/menus.md: `""` sits on the NUL of "WACK"/"ack" at 0x600a4/0x600a9), so its pool keeps the
  text an earlier literal left behind where our build has NUL padding. So each differing byte is checked
  for a dword anywhere in the image holding its address: none -> a dead pool byte (SAME, printed as a
  note), some -> a real difference (printed with both sides, exit 1).

Bytes a fixup would fill are masked out of the search and the compare: the linker writes an address
there and objmatch already checks those addresses.

Finally the gap census: the game's data bytes that no group of ours covers (non-zero ranges outside our
data groups, largest first). Those are initialized values our build does not emit - `extern` in ww.c,
supplied at link time, and a pure-C build would have to define them.

Exit 0 when nothing addressed differs, 1 otherwise.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
import libmatch as L                                 # noqa: E402

MINRUN, GAP = 16, 8


def classes(recs):
    """{group name: class name} from the segment-definition records (0x98/0x99)"""
    lnames, out = [None], {}
    for t, b in recs:
        if t == 0x96:
            p = 0
            while p < len(b):
                n = b[p]
                lnames.append(b[p + 1:p + 1 + n].decode('latin1'))
                p += 1 + n
    for t, b in recs:
        if t in (0x98, 0x99):
            p0 = 1 + (3 if b[0] >> 5 == 0 else 0)
            for lsize in ((4, 2) if t == 0x99 else (2, 4)):
                ln, p = L.index(b, p0 + lsize)
                cl, p = L.index(b, p)
                if 0 < ln < len(lnames) and 0 < cl < len(lnames) and p + 1 == len(b):
                    break
            if 0 < ln < len(lnames):
                out[lnames[ln]] = lnames[cl] if cl < len(lnames) else '?'
    return out


def windows(buf, mask, minlen, step=8):
    """windows of minlen bytes holding no fixup byte: [(start, end)]"""
    out, start = [], None
    for k in range(len(buf) + 1):
        free = k < len(buf) and not mask[k]
        if free and start is None:
            start = k
        elif not free and start is not None:
            out += [(a, a + minlen) for a in range(start, k - minlen + 1, step)]
            start = None
    return out


def printable(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)


def regions(d):
    """runs of consecutive-ish diff offsets: [(first, last)], a new run at >= GAP past the last"""
    out, s = [], d[0]
    for i in range(1, len(d)):
        if d[i] - d[i - 1] >= GAP:
            out.append((s, d[i - 1]))
            s = d[i]
    out.append((s, d[-1]))
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    verbose = '-v' in sys.argv[3:]
    prog, obj = sys.argv[1], sys.argv[2]
    le = os.path.join(ROOT, '.work', 'le')
    meta = json.load(open(os.path.join(le, prog + '.json')))
    imgs = {o['num']: (o['base'], open(os.path.join(le, '%s.obj%d.bin' % (prog, o['num'])), 'rb').read())
            for o in meta['objects']}
    (_, recs), = list(L.modules(open(obj, 'rb').read()))
    segs = L.code_of(recs, only_code=False)
    cls = classes(recs)
    bad = 0
    covered = []                                      # (obj num, start, end) in the game's image
    for seg in sorted(segs):
        if (cls.get(seg) or '').upper() == 'CODE':
            continue
        buf, mask = segs[seg]
        votes = {}
        for a, b in windows(buf, mask, MINRUN):
            pat = bytes(buf[a:b])
            for num, (ibase, img) in imgs.items():
                i = -1
                while True:                           # every occurrence: the first hit alone mis-votes
                    i = img.find(pat, i + 1)
                    if i < 0:
                        break
                    votes.setdefault((ibase + i - a, num), []).append((a, b))
        if not votes:
            print('%-8s %d bytes: no unmasked run of %d bytes occurs in the image - not compared'
                  % (seg, len(buf), MINRUN))
            continue
        (base, num), agreed = max(votes.items(), key=lambda kv: len(kv[1]))
        ibase, img = imgs[num]
        print('%-8s obj%d %d bytes, base 0x%06x (%d anchors agree, %d candidate bases, +%x..+%x)'
              % (seg, num, len(buf), base, len(agreed), len(votes),
                 min(a for a, _ in agreed), max(b for _, b in agreed)))
        covered.append((num, base - ibase, base - ibase + len(buf)))
        d = [k for k in range(len(buf))
             if not mask[k] and base - ibase + k < len(img) and img[base - ibase + k] != buf[k]]
        deadset = {k for k in d if not any(
            img2.find((base + k).to_bytes(4, 'little')) >= 0 for _, img2 in imgs.values())}
        print('         %d of %d compared bytes differ, %d of them addressed nowhere (dead pool bytes)'
              % (len(d), len(buf), len(deadset)))
        for a, b in regions(d):
            dead_region = all(k in deadset for k in d if a <= k <= b)
            if dead_region and not verbose:
                continue
            if not dead_region:
                bad += 1
            lo, hi = max(0, a - 4), min(len(buf), b + 5)
            print('    +%04x..+%04x (0x%06x) %s\n      ours  %s |%s|\n      game  %s |%s|'
                  % (a, b, base + a, 'dead pool byte' if dead_region else 'ADDRESSED - differs',
                     ' '.join('%02x' % buf[k] for k in range(lo, hi)), printable(buf[lo:hi]),
                     ' '.join('%02x' % img[base - ibase + k] for k in range(lo, hi)),
                     printable(img[base - ibase + lo:base - ibase + hi])))
    # the gap census: initialized bytes of the game in an object we put a data group into, but
    # outside every group of ours (library data and initialized globals our C declares `extern`)
    for num, (ibase, img) in imgs.items():
        if not any(n == num for n, _, _ in covered):
            continue
        runs, s = [], None
        for k in range(len(img) + 1):
            nz = k < len(img) and img[k]
            if nz and s is None:
                s = k
            elif not nz and s is not None:
                if not any(n == num and s < b2 and a2 < k for n, a2, b2 in covered):
                    runs.append((s, k))
                s = None
        merged = []
        for a, b in runs:
            if merged and a - merged[-1][1] < 0x20:
                merged[-1][1] = b
            else:
                merged.append([a, b])
        print('obj%d: %d initialized bytes of the game in %d region(s) no group of ours covers'
              % (num, sum(b - a for a, b in merged), len(merged)))
        for a, b in sorted(merged, key=lambda r: r[0] - r[1])[:8 if not verbose else len(merged)]:
            print('    0x%06x..0x%06x (%5d) |%s|' % (ibase + a, ibase + b, b - a,
                                                     printable(img[a:min(b, a + 40)])))
    print('%s' % ('OK: every addressed data byte equals the game\'s' if not bad
                  else '%d region(s) differ' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())

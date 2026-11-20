#!/usr/bin/env python3
"""assmatch.py: name WW.EXE's Apogee Sound System functions from the original ASS libraries.

usage: tools/wc/assmatch.py [--write]     (needs .work/le/ww.*, .work/le/ww_funcs.tsv and the original
                                           LIBs in .work/ext/audiolib/origlibs/*/AUDIO_WF.LIB,
                                           github.com/gamesrc-ver-recreation/audiolib)

Function by function (every PUBDEF/LPUBDEF of every module's code segment, up to the next one), over
every library version:
  1. exact: the function's bytes, fixup bytes masked, occur exactly once in WW.EXE's library range
     0x409EF-0x5BE2D -> CONFIRMED (byte-identical to <version> <module>);
  2. near: a function of a module that has an exact hit, searched at the function starts
     (tools/le/funcs.py) within 0x400 of where that hit puts it, scored by the share of equal
     unmasked bytes; the best start at >= 0.6 that no exact hit took -> LIKELY (score, version);
  3. free: every function still unplaced against the starts nothing covers, no anchor; best >= 0.7
     and 0.1 ahead of the next start -> LIKELY at >= 0.85, else SPECULATIVE.
Where two names fit one address (code shared by two drivers), the name whose module has the most
other hits within 0x2000 wins; the other is listed in the comment.
Prints names-file lines; --write replaces the block between the ASSMATCH markers in
analysis/ww_names.txt (idempotent)."""
import glob
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
import libmatch as L                    # noqa: E402
from lelib import Image                 # noqa: E402

LO, HI = 0x409EF, 0x5BE2E
NEAR_MIN, NEAR_WIN = 0.6, 0x400
FREE_MIN, FREE_GAP = 0.7, 0.1
BEGIN = '# ---- ASSMATCH begin (tools/wc/assmatch.py --write; do not edit by hand)'
END = '# ---- ASSMATCH end'


def pubdef(t, b, segs):
    """PUBDEF/LPUBDEF entries; some modules (the AWE32 SDK) write 32-bit offsets in 0x90 records."""
    for w in ((4,) if t & 1 else (2, 4)):
        try:
            got, p = [], 0
            _, p = L.index(b, p)
            si, p = L.index(b, p)
            if si == 0:
                p += 2
            while p < len(b):
                n = b[p]
                nm = b[p + 1:p + 1 + n].decode('latin1')
                p += 1 + n
                off = struct.unpack_from('<I' if w == 4 else '<H', b, p)[0]
                p += w
                _, p = L.index(b, p)
                got.append((segs[si], off, nm))
            if p == len(b):
                return got
        except (struct.error, IndexError):
            pass
    return []


def functions(lib):
    out = []
    for mod, recs in L.modules(open(lib, 'rb').read()):
        lnames, segs, pubs = [None], [None], []
        for t, b in recs:
            if t == 0x96:
                p = 0
                while p < len(b):
                    lnames.append(b[p + 1:p + 1 + b[p]].decode('latin1'))
                    p += 1 + b[p]
            elif t in (0x98, 0x99):
                q = 1 + (4 if t == 0x99 else 2) + (3 if b[0] >> 5 == 0 else 0)
                segs.append(lnames[L.index(b, q)[0]])
            elif t in (0x90, 0x91, 0xB6, 0xB7):
                pubs.extend(pubdef(t, b, segs))
        for sg, (buf, mask) in L.code_of(recs).items():
            ps = sorted((o, n) for s, o, n in pubs if s == sg)
            for k, (o, n) in enumerate(ps):
                e = ps[k + 1][0] if k + 1 < len(ps) else len(buf)
                out.append(dict(mod=os.path.basename(mod.replace('\\', '/')), name=n, off=o,
                                buf=buf[o:e], mask=mask[o:e]))
    return out


def plain(n):
    return n.strip('_')


def main(argv):
    img = Image('ww')
    code, base = img.code, img.code_base
    starts = sorted(int(l.split('\t')[0], 16) for l in open(os.path.join(ROOT, '.work', 'le', 'ww_funcs.tsv'))
                    if l.startswith('0x'))
    starts = [a for a in starts if LO <= a < HI]
    libs = sorted(glob.glob(os.path.join(ROOT, '.work', 'ext', 'audiolib', 'origlibs', '*', 'AUDIO_WF.LIB')))
    vers = {os.path.basename(os.path.dirname(p)): functions(p) for p in libs}
    exact = {}                                   # address -> {name: (version, module, size)}
    hits = {}                                    # version -> {name: address}
    for v, fs in vers.items():
        hits[v] = {}
        for f in fs:
            if len(f['buf']) < 8:
                continue
            h = [base + x for x in L.find(code, f['buf'], f['mask']) if LO <= base + x < HI]
            if len(h) == 1:
                hits[v][f['name']] = h[0]
                exact.setdefault(h[0], {}).setdefault(plain(f['name']), (v, f['mod'], len(f['buf'])))
    modat = {}
    for a, d in exact.items():
        for n, (v, mod, ln) in d.items():
            modat.setdefault(mod, []).append(a)
    near = {}                                    # address -> (score, name, version, module, size)
    for v, fs in vers.items():
        bymod = {}
        for f in fs:
            bymod.setdefault(f['mod'], []).append(f)
        for mod, fl in bymod.items():
            anchors = [(f, hits[v][f['name']]) for f in fl if f['name'] in hits[v]]
            if not anchors:
                continue
            for f in fl:
                if f['name'] in hits[v] or len(f['buf']) < 12:
                    continue
                fa, aa = min(anchors, key=lambda x: abs(x[0]['off'] - f['off']))
                pred = aa + f['off'] - fa['off']
                n = len(f['buf'])
                best = (0, None)
                for a in starts:
                    if abs(a - pred) > NEAR_WIN or a in exact or a + n > HI:
                        continue
                    o = a - base
                    tot = eq = 0
                    for i in range(n):
                        if not f['mask'][i]:
                            tot += 1
                            eq += code[o + i] == f['buf'][i]
                    r = eq / tot if tot else 0
                    if r > best[0]:
                        best = (r, a)
                r, a = best
                if a is not None and r >= NEAR_MIN and (a not in near or near[a][0] < r):
                    near[a] = (r, plain(f['name']), v, f['mod'], n)
    # a name placed twice by near: keep its best address
    byname = {}
    for a, x in near.items():
        if x[1] not in byname or near[byname[x[1]]][0] < x[0]:
            byname[x[1]] = a
    near = {a: x for a, x in near.items() if byname[x[1]] == a}
    exactnames = {n for d in exact.values() for n in d}
    near = {a: x for a, x in near.items() if x[1] not in exactnames}
    # 3. free: functions still unplaced against the function starts nothing covers yet, no anchor;
    # a candidate's extent (to the next start) must be 1/2..2x the function's size, the best score
    # must be >= FREE_MIN and beat the second best start by FREE_GAP
    span = []
    for a, d in exact.items():
        span.append((a, a + max(x[2] for x in d.values())))
    for a, x in near.items():
        span.append((a, a + x[4]))
    placed = exactnames | {x[1] for x in near.values()}
    free_starts = [a for a in starts if not any(s <= a < e for s, e in span)]
    nxt = {a: (starts[k + 1] if k + 1 < len(starts) else HI) for k, a in enumerate(starts)}
    free = {}
    for v, fs in vers.items():
        for f in fs:
            n = len(f['buf'])
            nm = plain(f['name'])
            if nm in placed or n < 24:
                continue
            scores = []
            for a in free_starts:
                ext = nxt[a] - a
                if not n // 2 <= ext <= 2 * n or a + n > HI:
                    continue
                o = a - base
                tot = eq = 0
                for i in range(n):
                    if not f['mask'][i]:
                        tot += 1
                        eq += code[o + i] == f['buf'][i]
                scores.append((eq / tot if tot else 0, a))
            scores.sort(reverse=True)
            if not scores or scores[0][0] < FREE_MIN:
                continue
            if len(scores) > 1 and scores[0][0] - scores[1][0] < FREE_GAP:
                continue
            r, a = scores[0]
            if a not in free or free[a][0] < r:
                free[a] = (r, nm, v, f['mod'], n)
    byname = {}
    for a, x in free.items():
        if x[1] not in byname or free[byname[x[1]]][0] < x[0]:
            byname[x[1]] = a
    free = {a: x for a, x in free.items() if byname[x[1]] == a}
    lines = [BEGIN,
             '# Apogee Sound System / GUS SDK functions named from the original ASS LIBs (iter 92): CONFIRMED =',
             '# byte-identical (fixups masked) to that version\'s function, LIKELY = anchored near match (score).']
    port = set()                                 # addresses the ASS port block names: never re-listed here
    nf = open(os.path.join(ROOT, 'analysis', 'ww_names.txt')).read()
    if '# ---- ASS port (' in nf:
        blk = nf[nf.index('# ---- ASS port ('):nf.index('# ---- ASS port end')]
        port = {int(l.split()[0], 16) for l in blk.splitlines() if l.startswith('0x')}
    for a in sorted((set(exact) | set(near) | set(free)) - port):
        if a in free:
            r, n, v, mod, ln = free[a]
            lines.append('0x%06x %-24s # %s: ASS %s %s free near match %.2f (%d bytes), no anchor; next best start %.2f lower or more'
                         % (a, n, 'LIKELY' if r >= 0.85 else 'SPECULATIVE', v, mod, r, ln, FREE_GAP))
            continue
        if a in exact:
            d = exact[a]
            ranked = sorted(d, key=lambda n: -sum(1 for b in modat[d[n][1]] if b != a and abs(b - a) < 0x2000))
            n = ranked[0]
            v, mod, ln = d[n]
            vs = sorted({x[0] for x in d.values()})
            also = ('; the same bytes are %s' % ', '.join(ranked[1:])) if len(ranked) > 1 else ''
            lines.append('0x%06x %-24s # CONFIRMED: byte-identical (fixups masked) to ASS %s %s, %d bytes%s'
                         % (a, n, '/'.join(vs), mod, ln, also))
        else:
            r, n, v, mod, ln = near[a]
            lines.append('0x%06x %-24s # LIKELY: ASS %s %s near match %.2f (%d bytes), anchored by its module\'s exact hits'
                         % (a, n, v, mod, r, ln))
    lines.append(END)
    nb = sum(max(x[2] for x in d.values()) for d in exact.values())
    summary = '%d exact (%d bytes), %d near, %d free' % (len(exact), nb, len(near), len(free))
    if '--write' in argv:
        p = os.path.join(ROOT, 'analysis', 'ww_names.txt')
        s = open(p).read()
        if BEGIN in s:
            s = s[:s.index(BEGIN)] + s[s.index(END) + len(END) + 1:]
        s = s.rstrip('\n') + '\n' + '\n'.join(lines) + '\n'
        open(p, 'w').write(s)
        print('analysis/ww_names.txt: ' + summary)
    else:
        print('\n'.join(lines))
        print(summary, file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

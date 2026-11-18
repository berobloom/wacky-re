#!/usr/bin/env python3
"""delink.py: everything of WW.EXE that is not the game's C module, as one OMF object.

usage: tools/link/delink.py [EXE] [SYMMAP] [OUT]
       (defaults original/WW.EXE, .work/link/symmap.txt, .work/link/BLOB.OBJ)

Segments (each holds the original bytes of its range):
  BEGTEXT    0x10000-0x10010   class CODE  (the clib null-call trap)
  BLOBTEXT   0x36A4E-end of obj1  class CODE  (the asm module and the libraries)
  BLOBDATA   0x60000-STACKLO   class DATA, DGROUP  (all of the data object: every module's data and BSS)
  BLOBSTACK  STACKLO-end of obj2  class STACK, DGROUP  (above `_end` 0x920f4: the 4 KB the LE header's
             ESP points to the top of)
The C module (0x10010-0x36A4D) is left out: our src/ww.c object takes its place.

References:
  - every LE fixup whose source lies in these ranges becomes an OMF fixup to the segment holding the
    target (the addend in the data bytes, as Watcom does); a target in the C module is an error (WW.EXE
    has none);
  - relative call/jmp rel32 from BLOBTEXT into the C module (not in the LE fixup table: one object)
    become self-relative fixups to the C function's public name (main_, xalloc_, xfree_), found by
    scanning for E8/E9/0F8x whose target is a function start our object defines;
  - every name in SYMMAP whose address lies in these ranges is published at its offset.
The start address is the LE entry point."""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
from lefix import fixups                # noqa: E402
import objmatch as M                    # noqa: E402

CSTART, CEND = 0x10010, 0x36A4E
STACKLO = 0x92100


def rec(t, body):
    b = bytes([t]) + struct.pack('<H', len(body) + 1) + body
    return b + bytes([(-sum(b)) & 0xFF])


def idx(i):
    return bytes([i]) if i < 0x80 else bytes([0x80 | (i >> 8), i & 0xFF])


def name(s):
    s = s.encode('latin1')
    return bytes([len(s)]) + s


class Seg:
    def __init__(self, sname, cls, lo, hi, data, group):
        self.sname, self.cls, self.lo, self.hi, self.data, self.group = sname, cls, lo, hi, data, group
        self.fix = {}                      # offset -> (kind, target): kind 'seg'/'ext', selfrel, loc
        self.pubs = []


def main(argv):
    exe = argv[0] if len(argv) > 0 else 'original/WW.EXE'
    smap = argv[1] if len(argv) > 1 else '.work/link/symmap.txt'
    out = argv[2] if len(argv) > 2 else '.work/link/BLOB.OBJ'
    os.chdir(ROOT)
    le, fx = fixups(exe)
    o1, o2 = le.objects[0], le.objects[1]
    img1 = open('.work/le/ww.obj1.bin', 'rb').read()
    img2 = open('.work/le/ww.obj2.bin', 'rb').read()
    end1 = o1['base'] + o1['vsize']
    end2 = o2['base'] + o2['vsize']

    def bytes_of(lo, hi):
        if lo >= o2['base']:
            b = img2[lo - o2['base']:hi - o2['base']]
        else:
            b = img1[lo - o1['base']:hi - o1['base']]
        return bytearray(b) + bytearray(hi - lo - len(b))

    whole = bool(os.environ.get('DELINK_WHOLE'))   # control: the game's own C too, one code segment
    if whole:
        code = [Seg('BLOBTEXT', 'CODE', 0x10000, end1, bytes_of(0x10000, end1), None)]
    else:
        code = [Seg('BEGTEXT', 'CODE', 0x10000, CSTART, bytes_of(0x10000, CSTART), None),
                Seg('BLOBTEXT', 'CODE', CEND, end1, bytes_of(CEND, end1), None)]
    TI = len(code) - 1                               # the segment holding the entry point

    # C functions taken from the game instead of our object (DELINK_ORIG=name,name; build.sh ORIG=):
    # one segment each, from its start to the next known C function start, extended back over its
    # switch jump table, which Watcom emits in front of the function (GOTCHAS)
    names = M.names('ww')
    ours = {}
    for line in open('.work/link/cpublics.txt'):
        n, a = line.split()
        ours[int(a, 16)] = n
    orig = [n for n in os.environ.get('DELINK_ORIG', '').split(',') if n]
    # our object's _TEXT and its publics, to check references into it
    import libmatch as L
    (_, orecs), = list(L.modules(open(os.path.join(os.path.dirname(out), 'WW.OBJ'), 'rb').read()))
    obuf, obmask = L.code_of(orecs)['_TEXT']
    ourtext = {n: o for sg_, o, n in M.publics(orecs) if sg_ == '_TEXT'}
    fname = dict(ours)                               # C address -> the name the link knows it by
    for n in orig:
        fname[names[n]] = n + '_'
    # function starts only (names-file labels such as jt_1a360 are not boundaries)
    cstarts = sorted(set(fname) | {CEND})
    origset = {names[n] for n in orig}
    osegs = []
    for n in orig:
        a = names[n]
        e = next(x for x in cstarts if x > a)
        prev = max((x for x in cstarts if x < a), default=CSTART)
        lo = a
        if prev not in origset:
            for src, kind, tgt in fx:
                if a <= src < e and prev <= tgt < a:
                    lo = min(lo, tgt)
        sg = Seg('O_' + n, 'CODE', lo, e, bytes_of(lo, e), None)
        sg.pubs.append((n + '_', a - lo))
        osegs.append(sg)
    if osegs:
        # the string literals Watcom placed in _TEXT in front of the first function: our object's
        # block lacks those of the functions taken out, so the game functions get the game's copy
        first = min(x for x in cstarts if x > CSTART)
        osegs.append(Seg('O__CSTRS', 'CODE', CSTART, first, bytes_of(CSTART, first), None))
    segs = code + osegs + [Seg('BLOBDATA', 'DATA', o2['base'], STACKLO, bytes_of(o2['base'], STACKLO), 'DGROUP'),
                           Seg('BLOBSTACK', 'STACK', STACKLO, end2, bytes_of(STACKLO, end2), 'DGROUP')]
    DI = len(code) + len(osegs)                      # BLOBDATA
    exts = []

    def ext(n):
        if n not in exts:
            exts.append(n)
        return exts.index(n) + 1

    def seg_of(a, allow_end=False):
        for i, s in enumerate(segs):
            if s.lo <= a < s.hi or (allow_end and a == s.hi):
                return i
        return None

    # 1. LE fixups
    seen = set()
    dropped = 0
    for src, kind, tgt in fx:
        if src in seen:
            continue
        seen.add(src)
        si = seg_of(src)
        if si is None:
            continue                       # a source in the C module: our object has its own
        s = segs[si]
        off = src - s.lo
        ti = seg_of(tgt, allow_end=True)
        if ti is None and kind == 'off32' and tgt in fname:
            struct.pack_into('<I', s.data, off, 0)          # a C function's address, by name
            s.fix[off] = ('ext', ext(fname[tgt]), False, 9)
            continue
        if ti is None and s.sname.startswith('O_') and s.hi <= tgt < next((x for x in cstarts if x > s.hi), CEND):
            dropped += 1                   # the next function's jump table at our tail: its own copy is used
            continue
        if ti is None and kind == 'off32' and CSTART <= tgt < CEND:
            # data inside one of our C functions (a table, or the string block in front of the first
            # function): that function's public + offset, checked against our object's bytes there
            f = max((x for x in fname if x <= tgt), default=min(fname))
            nm = fname[f]
            if nm not in ourtext:
                raise SystemExit('fixup at 0x%x targets 0x%x inside %s, not in our _TEXT' % (src, tgt, nm))
            q = ourtext[nm] + tgt - f
            if any(not obmask[q + k] and obuf[q + k] != img1[tgt - o1['base'] + k] for k in range(16)):
                raise SystemExit('fixup at 0x%x targets 0x%x = %s%+d: our bytes there differ'
                                 % (src, tgt, nm, tgt - f))
            struct.pack_into('<i', s.data, off, tgt - f)
            s.fix[off] = ('ext', ext(nm), False, 9)
            print('  inner ref 0x%x -> %s%+d (bytes agree)' % (src, nm, tgt - f))
            continue
        if ti is None:
            raise SystemExit('fixup at 0x%x targets 0x%x in the C module' % (src, tgt))
        if kind == 'off32':
            struct.pack_into('<I', s.data, off, tgt - segs[ti].lo)
            s.fix[off] = ('seg', ti, False, 9)
        else:                              # sel16: the 16-bit selector of the target's object
            struct.pack_into('<H', s.data, off, 0)
            if not os.environ.get('DELINK_NO_SEL16'):     # debugging: leave the selector unfixed
                s.fix[off] = ('seg', ti, False, 2)

    # 2. rel32 leaving a segment: BLOBTEXT into the C module (scanned), and every call/jmp/jcc of a
    # game C function (decoded) to a C function by name or to the library through a local public
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def rel32(s, i):
        if s.data[i] in (0xE8, 0xE9):
            return i + 1, 5
        if s.data[i] == 0x0F and i + 1 < len(s.data) and 0x80 <= s.data[i + 1] <= 0x8F:
            return i + 2, 6
        return None

    t = segs[TI]
    for i in range(0 if whole else len(t.data) - 5):
        r = rel32(t, i)
        if not r:
            continue
        at, ln = r
        if at + 4 > len(t.data) or any(k in t.fix for k in range(at - 3, at + 4)):
            continue
        dst = (t.lo + i + ln + struct.unpack_from('<i', t.data, at)[0]) & 0xFFFFFFFF
        if CSTART <= dst < CEND and dst in fname:
            n = fname[dst]
            struct.pack_into('<I', t.data, at, 0)
            t.fix[at] = ('ext', ext(n), True, 9)
            print('rel32 0x%x -> %s' % (t.lo + i, n))
    for s in osegs:
        pc = 0
        while pc < len(s.data):
            if pc in s.fix:                # a table entry or an address operand start: data
                pc += 4
                continue
            ins = next(md.disasm(bytes(s.data[pc:pc + 16]), s.lo + pc), None)
            if ins is None:
                pc += 1
                continue
            r = rel32(s, pc) if ins.size in (5, 6) else None
            if r and r[1] == ins.size:
                at = r[0]
                dst = (s.lo + pc + ins.size + struct.unpack_from('<i', s.data, at)[0]) & 0xFFFFFFFF
                if not s.lo <= dst < s.hi:
                    bi = seg_of(dst)
                    if dst in fname:
                        n = fname[dst]             # a C function, ours or the game's, by name
                    elif bi is not None and bi < DI:
                        # the library, or code inside another game segment (fade_out's loop tail
                        # sits in pic_show's range): a local public there
                        n = 'blob_%x' % dst
                        if (n, dst - segs[bi].lo) not in segs[bi].pubs:
                            segs[bi].pubs.append((n, dst - segs[bi].lo))
                    else:
                        raise SystemExit('%s: rel32 at 0x%x to 0x%x, not a function start or a game segment'
                                         % (s.sname, s.lo + pc, dst))
                    struct.pack_into('<I', s.data, at, 0)
                    s.fix[at] = ('ext', ext(n), True, 9)
            pc += ins.size
    if orig:
        print('from the game: %s (%d jump-table entries of following functions dropped)'
              % (' '.join('%s 0x%x-0x%x' % (s.sname[2:], s.lo, s.hi) for s in osegs), dropped))

    # 3. publics
    count = 0
    for line in open(smap):
        n, a, src = line.split()
        if a == '-':
            continue
        a = int(a, 16)
        si = seg_of(a)
        if si is None:
            continue
        if any(n == q for q, _ in segs[si].pubs):
            continue                       # a game function from DELINK_ORIG, already public
        segs[si].pubs.append((n, a - segs[si].lo))
        count += 1
    if not any(n == '_cstart_' for s in segs for n, _ in s.pubs):
        segs[TI].pubs.append(('_cstart_', le.eip + o1['base'] - segs[TI].lo))
    # __argc: pulled in by Watcom for main(argc, argv), never addressed by our code (symmap
    # 'unreferenced'); published at the start of BLOBDATA only so that the link resolves.
    if not whole:
        segs[DI].pubs.append(('__argc', 0))

    # debugging: DELINK_DROP=fix,stack,start leaves those parts out (to bisect a linker failure)
    drop = set(filter(None, os.environ.get('DELINK_DROP', '').split(',')))
    if 'fix' in drop:
        for sg in segs:
            sg.fix = {}
        exts = []
    rng = os.environ.get('DELINK_FIXRANGE')         # debugging: keep only fixups lo:hi (global order)
    if rng:
        lo, hi = (int(x) for x in rng.split(':'))
        k = 0
        for sg in segs:
            keep = {}
            for o in sorted(sg.fix):
                if lo <= k < hi:
                    keep[o] = sg.fix[o]
                k += 1
            sg.fix = keep
    if 'stack' in drop:
        segs = segs[:DI + 1]

    # 4. write OMF
    lnames = ['', 'FLAT', 'DGROUP', 'CODE', 'DATA', 'STACK'] + [s.sname for s in segs]
    li = {n: i + 1 for i, n in enumerate(lnames)}
    b = rec(0x80, name('wwblob'))
    # Watcom's own comment records, as WCC386 writes them: target OS (class 0xAA "OS220") and memory
    # model (class 0x9B "3fOp": 386, flat). Without them WLINK 9.5b page-faults on the first fixup.
    b += rec(0x88, b'\x80\xaaOS220')
    b += rec(0x88, b'\x80\x9b3fOp')
    b += rec(0x96, b''.join(name(n) for n in lnames))
    for s in segs:
        attr = 0x69 if s.cls == 'CODE' else (0x75 if s.cls == 'STACK' else 0x69)
        b += rec(0x99, bytes([attr]) + struct.pack('<I', s.hi - s.lo) + idx(li[s.sname]) + idx(li[s.cls]) + idx(1))
    b += rec(0x9A, idx(li['FLAT']))
    b += rec(0x9A, idx(li['DGROUP']) + b''.join(b'\xff' + idx(i + 1) for i, s in enumerate(segs) if s.group == 'DGROUP'))
    FLATG = 1
    if exts:
        b += rec(0x8C, b''.join(name(n) + idx(0) for n in exts))
    for i, s in enumerate(segs):
        grp = 2 if s.group == 'DGROUP' else FLATG
        for k in range(0, len(s.pubs), 20):
            body = idx(grp) + idx(i + 1)
            for n, off in s.pubs[k:k + 20]:
                body += name(n) + struct.pack('<I', off) + idx(0)
            b += rec(0x91, body)
    for i, s in enumerate(segs):
        fixed = sorted(s.fix)
        width = {o: (4 if s.fix[o][3] == 9 else 2) for o in fixed}
        p = 0
        n = len(s.data)
        while p < n:
            q = min(p + 1024, n)
            for o in fixed:                # never split a fixup field between two records
                if o < q < o + width[o]:
                    q = o
            chunk = bytes(s.data[p:q])
            fs = [o for o in fixed if p <= o < q]
            if any(chunk) or fs:
                b += rec(0xA1, idx(i + 1) + struct.pack('<I', p) + chunk)
                if fs:
                    body = b''
                    for o in fs:
                        kind, tgt, selfrel, loc = s.fix[o]
                        rel = o - p
                        body += bytes([0x80 | (0 if selfrel else 0x40) | (loc << 2) | (rel >> 8), rel & 0xFF])
                        tmeth = 0 if kind == 'seg' else 2
                        # target: a segment index (1-based in OMF) or an EXTDEF index (already 1-based)
                        body += bytes([(1 << 4) | 0x04 | tmeth]) + idx(FLATG) + idx(tgt + 1 if kind == 'seg' else tgt)
                    b += rec(0x9D, body)
            p = q
    st = le.eip + o1['base'] - segs[TI].lo
    if 'start' in drop:
        b += rec(0x8B, bytes([0x00]))
    else:
        b += rec(0x8B, bytes([0xC1, (1 << 4) | 0]) + idx(FLATG) + idx(TI + 1) + struct.pack('<I', st))
    open(out, 'wb').write(b)
    print('%s: %d bytes; %d fixups, %d rel32 into C (%s), %d publics; start BLOBTEXT+0x%x'
          % (out, len(b), sum(len(s.fix) for s in segs), sum(1 for s in segs for f in s.fix.values() if f[0] == 'ext'),
             ' '.join(exts), sum(len(s.pubs) for s in segs), st))


if __name__ == '__main__':
    main(sys.argv[1:])

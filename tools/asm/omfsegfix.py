#!/usr/bin/env python3
"""omfsegfix.py OBJ EXTNAME: make nasm's `seg x` fixups linkable by WLINK 9.5b, in place.

nasm writes `mov bx, seg x` in a FLAT segment as a base (16-bit selector) fixup whose target is
*group FLAT* (fix data 0x55, target datum = the FLAT group). WLINK 9.5b page-faults on it with no
message and no output file (iter 90: a bisect over the 41 FIXUPP records of src/wwasm.asm's object
found the one record holding it; every other record links). This rewrites each such fixup to
target = the extern EXTNAME, frame = group FLAT (fix data 0x16), the form tools/link/delink.py writes
for the game's own sel16 fixups. WW.EXE has one: 0x4090e in kbd_isr, `mov bx, seg _kbd_busy` (build.sh reads the name from that line).

Nothing else is needed for a nasm object (iter 90, each tried alone and linked): nasm's 16-bit
SEGDEF/LEDATA/PUBDEF forms and its own COMENT records link as they are, and the object links without
WCC386's OS220/3fOp COMENTs (which a delink.py-style hand-written object needs, GOTCHAS)."""
import struct
import sys


def rec(t, body):
    b = bytes([t]) + struct.pack('<H', len(body) + 1) + body
    return b + bytes([(-sum(b)) & 0xFF])


def idxlen(b, q):
    return 1 if b[q] < 0x80 else 2


def fix_base(body, extidx):
    """One FIXUPP body: base fixups targeting group FLAT -> target extern extidx, frame FLAT."""
    out, q, n = b'', 0, 0
    while q < len(body):
        b = body[q]
        if not b & 0x80:
            raise SystemExit('THREAD subrecord: not handled')
        loc, fd = body[q:q + 2], body[q + 2]
        q += 3
        ln = 0
        if not fd & 0x80 and (fd >> 4) & 7 < 3:
            ln += idxlen(body, q + ln)
        if not fd & 0x08:
            ln += idxlen(body, q + ln)
        if not fd & 0x04:
            ln += 4
        if (b >> 2) & 15 == 2 and fd == 0x55 and body[q] == 1:
            out += loc + bytes([0x16, 1]) + (bytes([extidx]) if extidx < 0x80
                                             else bytes([0x80 | extidx >> 8, extidx & 0xFF]))
            n += 1
        else:
            out += loc + bytes([fd]) + body[q:q + ln]
        q += ln
    return out, n


def main(path, extname):
    d = open(path, 'rb').read()
    out, p, exts, n = b'', 0, [], 0
    while p < len(d):
        t, ln = d[p], struct.unpack_from('<H', d, p + 1)[0]
        body = d[p + 3:p + 3 + ln - 1]
        r = d[p:p + 3 + ln]
        p += 3 + ln
        if t == 0x8C:
            q = 0
            while q < len(body):
                exts.append(body[q + 1:q + 1 + body[q]].decode())
                q += 1 + body[q]
                q += idxlen(body, q)
        if t in (0x9C, 0x9D):
            nb, k = fix_base(body, exts.index(extname) + 1)
            n += k
            r = rec(t, nb)
        out += r
    open(path, 'wb').write(out)
    print('%s: %d seg fixup(s) -> %s' % (path, n, extname))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))

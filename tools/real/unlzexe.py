#!/usr/bin/env python3
"""Unpack an LZEXE 0.91 executable into a plain MZ .EXE (for Ghidra/REA).

  unlzexe.py PACKED.EXE OUT.EXE

Writes an MZ header with the original entry, stack and relocation table,
then the unpacked image (segment words unrelocated, as in a normal EXE).
Checked on Dangerous Dave: the image equals RAM at the unpacked entry.
LZEXE 0.90 (LZ09) is not handled.
"""
import struct, sys

def unpack(f):
    if f[:2] != b"MZ" or f[0x1C:0x20] != b"LZ91":
        raise SystemExit("not LZEXE 0.91")
    hdr = struct.unpack_from("<H", f, 8)[0] * 16
    stub = hdr + struct.unpack_from("<H", f, 0x16)[0] * 16
    ip, cs, sp, ss = struct.unpack_from("<4H", f, stub)
    p = hdr; out = bytearray()
    w = n = 0
    def fill():
        nonlocal p, w, n
        w = f[p] | f[p + 1] << 8; p += 2; n = 16
    def bit():
        nonlocal w, n
        b = w & 1; w >>= 1; n -= 1
        if n == 0: fill()
        return b
    def byte():
        nonlocal p
        b = f[p]; p += 1; return b
    fill()
    while True:
        if bit(): out.append(byte()); continue
        if not bit():
            ln = (bit() << 1 | bit()) + 2; span = byte() - 0x100
        else:
            lo = byte(); hi = byte()
            span = (lo | (hi & 0xF8) << 5 | 0xE000) - 0x10000
            ln = hi & 7
            if ln: ln += 2
            else:
                ln = byte()
                if ln == 0: break
                if ln == 1: continue
                ln += 1
        for _ in range(ln): out.append(out[len(out) + span])
    relocs = []; p = stub + 0x158; seg = ofs = 0
    while True:
        span = f[p]; p += 1
        if span == 0:
            span = f[p] | f[p + 1] << 8; p += 2
            if span == 0: seg += 0x0FFF; continue
            if span == 1: break
        ofs += span; seg += ofs >> 4; ofs &= 0xF
        relocs.append((ofs, seg))
    return bytes(out), relocs, (ip, cs, sp, ss)

def main():
    f = open(sys.argv[1], "rb").read()
    img, relocs, (ip, cs, sp, ss) = unpack(f)
    hdr_len = (0x1C + 4 * len(relocs) + 15) // 16 * 16
    total = hdr_len + len(img)
    minalloc = struct.unpack_from("<H", f, 0x0A)[0]
    h = bytearray(hdr_len)
    struct.pack_into("<2s13H", h, 0, b"MZ", total % 512, (total + 511) // 512,
                     len(relocs), hdr_len // 16, minalloc, 0xFFFF, ss, sp, 0, ip, cs, 0x1C, 0)
    for i, (o, s) in enumerate(relocs):
        struct.pack_into("<HH", h, 0x1C + 4 * i, o, s)
    open(sys.argv[2], "wb").write(bytes(h) + img)
    print("%s: image 0x%X bytes, %d relocations, entry %04X:%04X, stack %04X:%04X"
          % (sys.argv[2], len(img), len(relocs), cs, ip, ss, sp))

if __name__ == "__main__":
    main()

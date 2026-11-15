#!/usr/bin/env python3
"""Identify a DOS executable: MZ header, packer hints, bound LE/LX/NE/PE.

usage: mz.py FILE [FILE...]

Prints the MZ fields at their real offsets (entry CS:IP is at 0x14/0x16;
reading it from 0x1E or later reads the relocation table), the load image
size, the relocation count, an embedded new-header (LE/LX/NE/PE at e_lfanew
0x3C, or an LE found later in a bound DOS/4GW stub), and the usual packer
signatures. An absurd header means wrong offsets, not a strange format.
"""
import hashlib
import struct
import sys

PACKERS = [(b'LZ09', 'LZEXE 0.90'), (b'LZ91', 'LZEXE 0.91'),
           (b'PKLITE', 'PKLITE'), (b'RNC', 'ProPack RNC'),
           (b'UPX!', 'UPX'), (b'Packed file is corrupt', 'EXEPACK'),
           (b'DOS/4G', 'DOS/4G(W) stub'), (b'PMODE/W', 'PMODE/W'),
           (b'CauseWay', 'CauseWay'), (b'DOS32A', 'DOS/32A'),
           (b'WATCOM', 'Watcom runtime'), (b'Borland', 'Borland runtime'),
           (b'Microsoft', 'Microsoft runtime')]


def main(argv):
    for path in argv[1:]:
        d = open(path, 'rb').read()
        print('== %s  %d bytes  sha256 %s' % (
            path, len(d), hashlib.sha256(d).hexdigest()[:16]))
        if d[:2] not in (b'MZ', b'ZM'):
            print('   not MZ (a .COM image loads at PSP:0100)')
            continue
        (cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs,
         lfarlc, ovno) = struct.unpack_from('<13H', d, 2)
        size = (cp - 1) * 512 + (cblp or 512) if cp else 0
        print('   image %d bytes (file %d), header %d paras = %d bytes' % (
            size, len(d), cparhdr, cparhdr * 16))
        print('   entry CS:IP %04X:%04X  SS:SP %04X:%04X  (relative to the '
              'load segment)' % (cs, ip, ss, sp))
        print('   relocs %d at 0x%X, min/max alloc %X/%X paras, overlay %d' % (
            crlc, lfarlc, minalloc, maxalloc, ovno))
        print('   load image starts at file 0x%X; the entry segment\'s '
              'offset = file offset - 0x%X' % (cparhdr * 16,
                                               cparhdr * 16 + cs * 16))
        if size < len(d):
            print('   %d bytes after the load image (overlay / bound '
                  'program / data)' % (len(d) - size))
        if lfarlc >= 0x40 and len(d) > 0x40:
            off = struct.unpack_from('<I', d, 0x3C)[0]
            if 0 < off < len(d) - 2 and d[off:off + 2] in (
                    b'LE', b'LX', b'NE', b'PE'):
                print('   new header %s at 0x%X' % (d[off:off + 2].decode(),
                                                    off))
        for sig in (b'LE\0\0', b'LX\0\0'):
            p = d.find(sig, size if size < len(d) else 0x40)
            if p > 0:
                print('   %s header candidate at 0x%X (bound extender)' % (
                    sig[:2].decode(), p))
                break
        found = [name for sig, name in PACKERS if sig in d[:0x20000]]
        if found:
            print('   strings: ' + ', '.join(found))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

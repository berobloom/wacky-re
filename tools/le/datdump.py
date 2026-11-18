#!/usr/bin/env python3
"""Extract a WACKY.DAT entry.  The directory is 2 + a word count, records of 22
bytes (char name[14]; long size; long offset) blank-terminated, and `offset` is
relative to file offset 2 (findings/formats.md).

usage: datdump.py --selfcheck
       datdump.py NAME [outfile]        (hex to stdout if no outfile)
"""
import binascii
import struct
import sys

DAT = 'original/WACKY.DAT'


def entries(b):
    n = struct.unpack_from('<H', b, 0)[0]
    out = []
    for i in range(n):
        r = b[2 + i * 22: 2 + (i + 1) * 22]
        name = r[:14].split(b'\0')[0].decode('latin1')
        if not name:
            break                       # blank-terminated, not count-terminated
        size, off = struct.unpack_from('<ii', r, 14)
        out.append((name, size, off))
    return out


def get(b, ents, name):
    for nm, size, off in ents:
        if nm.upper() == name.upper():
            return b[off + 2: off + 2 + size]
    raise SystemExit('%s not among %d entries' % (name, len(ents)))


def main():
    b = open(DAT, 'rb').read()
    ents = entries(b)
    if sys.argv[1:] == ['--selfcheck']:
        assert len(ents) == struct.unpack_from('<H', b, 0)[0], 'terminator early'
        end = 2 + (len(ents) + 1) * 22
        assert ents[0][2] + 2 == end, 'entry 0 does not start after the directory'
        last = max(o + 2 + s for _, s, o in ents)
        assert last == len(b), 'records do not tile the file (%d vs %d)' % (last, len(b))
        print('OK %d entries, %d bytes' % (len(ents), len(b)))
        return
    data = get(b, ents, sys.argv[1])
    if len(sys.argv) > 2:
        open(sys.argv[2], 'wb').write(data)
    sys.stderr.write('%s: %d bytes\n' % (sys.argv[1], len(data)))
    if len(sys.argv) < 3:
        sys.stdout.write(binascii.hexlify(data).decode() + '\n')


if __name__ == '__main__':
    main()

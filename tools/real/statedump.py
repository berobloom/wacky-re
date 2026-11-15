#!/usr/bin/env python3
"""Read guest RAM out of a DOSBox-X save state, without the emulator.

A .state file is a ZIP. Its member named exactly `Memory` (not
`Memory_Size`, which also matches a substring test) is a 136-byte header
followed by contiguous guest linear RAM from address 0. So every save state
is a full RAM dump: globals, tables, back buffers and code, readable offline.

The VGA framebuffer is NOT in it: the A000 page reads 0xFF and the `Vga`
member holds no raw pixel rows. Compare the game's own back buffer instead.

Segment bases differ per boot (the DOS environment block moves the load
segment): find them per state, never carry them across boots.

usage:
  statedump.py STATE --get LIN:LEN[,LIN:LEN...]    hexdump linear ranges
  statedump.py STATE --slurp LIN:LEN out.bin        write raw bytes to a file
  statedump.py STATE --find HEXBYTES [LIN:LEN]      linear addresses of a pattern
  statedump.py STATE --seg SEG:OFF:LEN              hexdump at segment:offset
Numbers are hex (with or without 0x).
"""
import sys
import zipfile

MEMBER, HDR = 'Memory', 136


def memory(path):
    return zipfile.ZipFile(path).read(MEMBER)


def lin(m, a, n=1):
    return m[HDR + a:HDR + a + n]


def hx(s):
    return int(s, 16)


def rng(s):
    a, n = s.split(':')
    return hx(a), hx(n)


def dump(m, a, n):
    b = lin(m, a, n)
    for i in range(0, len(b), 16):
        print('%05X  %s' % (a + i, b[i:i + 16].hex(' ')))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    m = memory(argv[1])
    cmd = argv[2]
    if cmd == '--get':
        for r in argv[3].split(','):
            dump(m, *rng(r))
    elif cmd == '--slurp':
        a, n = rng(argv[3])
        open(argv[4], 'wb').write(lin(m, a, n))
    elif cmd == '--find':
        pat = bytes.fromhex(argv[3])
        a, n = rng(argv[4]) if len(argv) > 4 else (0, len(m) - HDR)
        body = lin(m, a, n)
        p, hits = body.find(pat), 0
        while p >= 0 and hits < 200:
            print('%05X' % (a + p))
            hits += 1
            p = body.find(pat, p + 1)
    elif cmd == '--seg':
        seg, off, n = (hx(x) for x in argv[3].split(':'))
        print('linear = %05X' % (seg * 16 + off))
        dump(m, seg * 16 + off, n)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

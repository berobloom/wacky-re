#!/usr/bin/env python3
"""racecmp.py: the race itself, frame by frame, the port against the original.

usage: port/tools/racecmp.py ORIG.bin PORT.bin [FRAMES]       -> SAME ... / DIFF ... (exit 1)
       port/tools/racecmp.py --from-watch WATCH.log OUT.bin   (make ORIG.bin)

A trace is one record per race frame, taken at race_run's `ticks2++` (the game's 0x3114c `inc
[0x7ec64]`; src/ww.c race_run): ticks2 (4 bytes), the eight player records `players` 0x6aa90
(0x340 bytes) and the start order v70720 0x70720 (16 bytes). The port writes it from gdb
(port/check.sh step 4, `append binary memory`); the original's comes from
`tools/dosrun.py watch 0x3114c+codedelta 0x7ec64+d:4,0x6aa90+d:0x340,0x70720+d:0x10`, converted with
--from-watch. The race steps once per frame (ticks only pace the display), so the frames compare
whatever the speed; the start order depends on the grid seed (racetime at fn_1be3c), which the
caller makes equal. Every byte must be equal except players[].f5e (+0x5e, a pointer into a heap
table, `*(int *)lit72350 + ...`), which must differ by one constant: the two heaps' bases.
"""
import re
import struct
import sys

REC = 4 + 0x340 + 0x10


def from_watch(log, out):
    seen = {}
    for line in open(log):
        m = re.search(r'257c64:([0-9a-f]{8})\s+243a90:([0-9a-f]+)\s+249720:([0-9a-f]+)', line)
        if not m:
            m = re.search(r':([0-9a-f]{8})\s+[0-9a-f]+:([0-9a-f]{1664})\s+[0-9a-f]+:([0-9a-f]{32})', line)
        if m:
            seen.setdefault(struct.unpack('<i', bytes.fromhex(m.group(1)))[0],
                            bytes.fromhex(m.group(1) + m.group(2) + m.group(3)))
    with open(out, 'wb') as f:
        for k in sorted(seen):
            f.write(seen[k])
    print('%d frames (%d..%d) -> %s' % (len(seen), min(seen), max(seen), out))


def load(path):
    b = open(path, 'rb').read()
    d = {}
    for i in range(0, len(b) - REC + 1, REC):
        d.setdefault(struct.unpack('<i', b[i:i + 4])[0], b[i + 4:i + REC])
    return d


def main(argv):
    if argv[:1] == ['--from-watch']:
        return from_watch(argv[1], argv[2])
    a, b = load(argv[0]), load(argv[1])
    frames = int(argv[2]) if len(argv) > 2 else None
    ks = sorted(k for k in set(a) & set(b) if frames is None or k < frames)
    if not ks or ks[0] != 0:
        print('DIFF no common frames from 0 (orig %d, port %d)' % (len(a), len(b)))
        return 1
    if a[0][-16:] != b[0][-16:]:
        print('DIFF start order orig %s port %s (seed)' % (struct.unpack('<8h', a[0][-16:]),
                                                          struct.unpack('<8h', b[0][-16:])))
        return 1
    delta = None
    for k in ks:
        for o in range(0, 0x340, 2):
            if o % 0x68 in (0x5e, 0x60):
                continue
            if a[k][o:o + 2] != b[k][o:o + 2]:
                print('DIFF frame %d player %d +0x%02x: orig %d port %d' % (
                    k, o // 0x68, o % 0x68, struct.unpack('<h', a[k][o:o + 2])[0],
                    struct.unpack('<h', b[k][o:o + 2])[0]))
                return 1
        for p in range(8):
            o = p * 0x68 + 0x5e
            d = struct.unpack('<i', b[k][o:o + 4])[0] - struct.unpack('<i', a[k][o:o + 4])[0]
            if delta is None:
                delta = d
            elif d != delta:
                print('DIFF frame %d player %d f5e: pointer moved differently (delta %#x, was %#x)'
                      % (k, p, d, delta))
                return 1
    if len(ks) < (frames or 1):
        print('DIFF only %d frames in common' % len(ks))
        return 1
    print('SAME  %d frames, 8 players, f5e heap delta %#x' % (len(ks), delta & 0xffffffff))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

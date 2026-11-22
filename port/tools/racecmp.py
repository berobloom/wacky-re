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
MAGIC = b'RCK1'         # a trace whose records also hold karts[] 0x68410 (8 x 0x104 bytes)
KARTS = 0x820
REC_K = REC + KARTS


def from_watch(log, out):
    seen, karts = {}, None
    for line in open(log):
        blocks = re.findall(r'\b[0-9a-f]+:([0-9a-f]{8,})', line.split('eip', 1)[-1])
        if len(blocks) < 3 or len(blocks[0]) != 8:
            continue
        if karts is None:
            karts = len(blocks) > 3
        seen.setdefault(struct.unpack('<i', bytes.fromhex(blocks[0]))[0],
                        bytes.fromhex(''.join(blocks[:4 if karts else 3])))
    with open(out, 'wb') as f:
        if karts:
            f.write(MAGIC)
        for k in sorted(seen):
            f.write(seen[k])
    print('%d frames (%d..%d)%s -> %s' % (len(seen), min(seen), max(seen),
                                          ' with karts' if karts else '', out))


def load(path):
    """{ticks2: record without ticks2}; records of an RCK1 file end with the karts"""
    b = open(path, 'rb').read()
    rec, start = (REC_K, 4) if b[:4] == MAGIC else (REC, 0)
    d = {}
    for i in range(start, len(b) - rec + 1, rec):
        d.setdefault(struct.unpack('<i', b[i:i + 4])[0], b[i + 4:i + rec])
    return d


# kart pointer fields (32-bit): equal if both are 0, else orig - port is one constant per field over
# every kart and frame (--survey on captures/race_drive_ww.bin: these four were all that differed).
# f4e (&tb7b448[..]), f72 and f76 (surface records) point into the data object: their delta is the
# original boot's data delta (0x1d9000), the port maps the object at the game's own 0x60000. f4a
# (sparkdata) points past it (orig 0x26dd80 = game 0x94d80, the port's own copy at 0x0806a430).
KART_PTRS = (0x4a, 0x4e, 0x72, 0x76)
KART_SKIP = {o + i for o in KART_PTRS for i in (0, 2)}
kart_delta = {}


def kart_diffs(ra, rb):
    """(kart, offset, orig, port) for the 16-bit units of the karts block that differ"""
    a, b = ra[REC - 4:], rb[REC - 4:]
    for o in range(0, min(len(a), len(b)), 0x104):
        for f in KART_PTRS:
            x, y = (struct.unpack('<I', r[o + f:o + f + 4])[0] for r in (a, b))
            if x == y == 0:
                continue
            d = (x - y) & 0xffffffff
            if (x == 0) != (y == 0) or kart_delta.setdefault(f, d) != d:
                yield (o // 0x104, f, x, y)
    for o in range(0, min(len(a), len(b)), 2):
        if o % 0x104 not in KART_SKIP and a[o:o + 2] != b[o:o + 2]:
            yield (o // 0x104, o % 0x104, struct.unpack('<h', a[o:o + 2])[0],
                   struct.unpack('<h', b[o:o + 2])[0])


def survey(a, b, ks):
    first = {}
    for k in ks:
        for kart, off, x, y in kart_diffs(a[k], b[k]):
            first.setdefault(off, (k, kart, x, y, set()))[4].add(kart)
    for off in sorted(first):
        k, kart, x, y, ks_ = first[off]
        print('kart +0x%02x: first frame %d kart %d orig %d port %d; karts %s'
              % (off, k, kart, x, y, sorted(ks_)))
    print('%d kart offsets differ' % len(first))


def main(argv):
    if argv[:1] == ['--from-watch']:
        return from_watch(argv[1], argv[2])
    if argv[:1] == ['--survey']:
        a, b = load(argv[1]), load(argv[2])
        return survey(a, b, sorted(k for k in set(a) & set(b) if k < int(argv[3])))
    a, b = load(argv[0]), load(argv[1])
    frames = int(argv[2]) if len(argv) > 2 else None
    ks = sorted(k for k in set(a) & set(b) if frames is None or k < frames)
    if not ks or ks[0] != 0:
        print('DIFF no common frames from 0 (orig %d, port %d)' % (len(a), len(b)))
        return 1
    if a[0][0x340:0x350] != b[0][0x340:0x350]:
        print('DIFF start order orig %s port %s (seed)' % (struct.unpack('<8h', a[0][0x340:0x350]),
                                                          struct.unpack('<8h', b[0][0x340:0x350])))
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
        for kart, off, x, y in kart_diffs(a[k], b[k]):
            print('DIFF frame %d kart %d +0x%02x: orig %d port %d' % (k, kart, off, x, y))
            return 1
    if len(ks) < (frames or 1):
        print('DIFF only %d frames in common' % len(ks))
        return 1
    karts = len(a[ks[0]]) > REC - 4 and len(b[ks[0]]) > REC - 4
    print('SAME  %d frames, 8 players%s, f5e heap delta %#x' % (
        len(ks), ' and karts (pointer deltas %s)' % ' '.join(
            'f%02x %#x' % kv for kv in sorted(kart_delta.items())) if karts else '',
        delta & 0xffffffff))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

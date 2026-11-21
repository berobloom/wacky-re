#!/usr/bin/env python3
"""shotcmp.py: compare a port screenshot with one of the original (DOSBox, any integer scale).

usage: port/tools/shotcmp.py PORT.png ORIG.png [DIFF.png]

Both are brought to the port's size (the original's capture is sampled at its scale, nearest
neighbour), then: mean absolute RGB difference, and the share of pixels whose largest channel
differs by more than 40. Prints `SAME`, `CLOSE` or `DIFF` with the numbers; exit 0 unless DIFF.
SAME: no pixel over 40. CLOSE: under 1% over 40 (DAC rounding, a moving cursor). DIFF.png marks the
differing pixels in red over a dimmed copy of the port's picture."""
import sys

from PIL import Image


def main(argv):
    a = Image.open(argv[0]).convert('RGB')
    b = Image.open(argv[1]).convert('RGB')
    sx, sy = b.width // a.width, b.height // a.height
    if sx < 1 or sy < 1 or b.width != a.width * sx or b.height != a.height * sy:
        sys.exit('sizes %dx%d and %dx%d are not an integer scale apart' % (a.width, a.height, b.width, b.height))
    if (sx, sy) != (1, 1):
        b = b.resize(a.size, Image.NEAREST, box=(0, 0, b.width, b.height))
    pa, pb = a.load(), b.load()
    tot, big = 0, 0
    marks = Image.eval(a, lambda v: v // 3)
    pm = marks.load()
    for y in range(a.height):
        for x in range(a.width):
            d = [abs(p - q) for p, q in zip(pa[x, y], pb[x, y])]
            tot += sum(d) / 3
            if max(d) > 40:
                big += 1
                pm[x, y] = (255, 0, 0)
    n = a.width * a.height
    share = 100.0 * big / n
    verdict = 'SAME' if big == 0 else 'CLOSE' if share < 1 else 'DIFF'
    print('%s  mean %.2f  over-40 %d px (%.2f%%)' % (verdict, tot / n, big, share))
    if len(argv) > 2:
        marks.save(argv[2])
    return 1 if verdict == 'DIFF' else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

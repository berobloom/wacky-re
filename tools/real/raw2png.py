#!/usr/bin/env python3
"""Indexed framebuffer (raw bytes) -> PNG.

usage: raw2png.py RAW PALETTE OUT.png [width=320] [scale=2] [--8bit]

PALETTE is 768 bytes of RGB (the last 768 bytes of the file are used): a VGA
DAC dump or a game .PAL/.COL file. VGA DAC values are 6-bit (0..63) and are
scaled by 4 unless --8bit is given. A PCX file's trailing palette is 8-bit.
"""
import sys

from PIL import Image


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    if len(args) < 3:
        print(__doc__)
        return 1
    raw, pal, out = args[:3]
    w = int(args[3]) if len(args) > 3 else 320
    scale = int(args[4]) if len(args) > 4 else 2
    k = 1 if '--8bit' in argv else 4
    d = open(raw, 'rb').read()
    p = open(pal, 'rb').read()[-768:]
    im = Image.new('P', (w, len(d) // w))
    im.putpalette([min(255, c * k) for c in p])
    im.putdata(list(d[:w * im.height]))
    im.resize((w * scale, im.height * scale), Image.NEAREST).save(out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

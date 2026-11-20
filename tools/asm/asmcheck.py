#!/usr/bin/env python3
"""asmcheck.py: is src/wwasm.asm the game's assembler module, byte for byte?

usage: tools/asm/asmcheck.py [ASM]       (default src/wwasm.asm; needs .work/le/ww.*)

1. nasm -DBIN -f bin (org 0x36a4e, every EXT name `equ` its game address) must give exactly the 41,377
   bytes of WW.EXE at 0x36A4E-0x409EE as the LE loader leaves them (fixups applied, .work/le/ww.obj1.bin);
2. nasm -f obj (EXT names `extern`) must assemble, and its LE-fixup sites must be the game's: every
   off32/sel16 fixup the game has in the range is a FIXUPP in the object at the same offset, and
   the object has no other fixups.
Exit 0 and one OK line when both hold; else the first differing addresses."""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'link'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
from lefix import fixups               # noqa: E402
from lelib import Image                # noqa: E402

LO, HI = 0x36A4E, 0x409EF


def omf_fixup_sites(path):
    """Offsets (in the one code segment) of every FIXUPP location in an OMF object."""
    d = open(path, 'rb').read()
    p, last, sites = 0, None, []
    while p < len(d):
        t, n = d[p], struct.unpack_from('<H', d, p + 1)[0]
        body = d[p + 3:p + 3 + n - 1]
        if t in (0xA0, 0xA1):              # LEDATA: segment index, offset
            q = 1 if body[0] < 0x80 else 2
            last = struct.unpack_from('<I' if t == 0xA1 else '<H', body, q)[0]
        elif t in (0x9C, 0x9D):            # FIXUPP
            q = 0
            while q < len(body):
                b = body[q]
                if b & 0x80 == 0:          # THREAD subrecord
                    q += 1
                    meth = (b >> 2) & 7
                    if (b & 0x40) == 0 and meth < 3 or (b & 0x40) and meth < 3:
                        q += 1 if body[q] < 0x80 else 2
                    continue
                loc = ((b & 3) << 8) | body[q + 1]
                sites.append(last + loc)
                q += 2
                fd = body[q]
                q += 1
                if not fd & 0x80:          # frame not by thread
                    if (fd >> 4) & 7 < 3:
                        q += 1 if body[q] < 0x80 else 2
                if not fd & 0x08:          # target not by thread
                    q += 1 if body[q] < 0x80 else 2
                if not fd & 0x04:          # displacement present
                    q += 4 if t == 0x9D else 2
        p += 3 + n
    return sites


def main(argv):
    asm = argv[0] if argv else os.path.join(ROOT, 'src', 'wwasm.asm')
    tmp = tempfile.mkdtemp()
    ok = True
    want = Image('ww').read(LO, HI - LO)
    r = subprocess.run(['nasm', '-DBIN', '-f', 'bin', '-o', os.path.join(tmp, 'b.bin'), asm],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[:2000])
        return 1
    got = open(os.path.join(tmp, 'b.bin'), 'rb').read()
    diff = [LO + k for k in range(max(len(got), len(want)))
            if k >= len(got) or k >= len(want) or got[k] != want[k]]
    if diff:
        ok = False
        print('BIN: %d bytes, game %d; %d differ, first at %s' % (
            len(got), len(want), len(diff), ' '.join(hex(a) for a in diff[:8])))
    r = subprocess.run(['nasm', '-f', 'obj', '-o', os.path.join(tmp, 'o.obj'), asm],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[:2000])
        return 1
    le, fx = fixups(os.path.join(ROOT, 'original', 'WW.EXE'))
    game = sorted(s - LO for s, k, t in fx if LO <= s < HI)
    ours = sorted(omf_fixup_sites(os.path.join(tmp, 'o.obj')))
    if game != ours:
        ok = False
        g, o = set(game), set(ours)
        print('OBJ fixup sites: game %d ours %d; game only %s; ours only %s' % (
            len(game), len(ours), ' '.join(hex(LO + a) for a in sorted(g - o)[:8]),
            ' '.join(hex(LO + a) for a in sorted(o - g)[:8])))
    if ok:
        print('OK: %s is WW.EXE 0x%X-0x%X byte for byte (%d bytes), %d fixup sites agree'
              % (os.path.relpath(asm, ROOT), LO, HI - 1, len(got), len(game)))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

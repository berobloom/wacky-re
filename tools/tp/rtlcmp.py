#!/usr/bin/env python3
"""Which routines of a target EXE also occur in reference EXEs built with a known compiler.

usage: rtlcmp.py TARGET.EXE REF.EXE[,REF2.EXE...] [SEG ...]

TARGET is the unpacked game; the REFs are test programs compiled with ONE candidate Turbo Pascal, together using
many runtime routines (comma-separated, searched as one). For every target routine (in the given segments, default
all; SEG = the target's segment value, e.g. 2CD1) the tool takes its bytes from the entry up to `reached` (the bytes
the descent decoded; literals behind them are left out) and classifies it:

  same     the bytes occur in a REF, with relocated words, call operands and absolute memory operands as wildcards
           (smart linking and the data layout move routines and variables; the code stays the same);
  layout   not `same`, but some REF has the same instruction sequence where only numbers differ (immediates that
           hold DS or CS offsets, branch targets outside the routine): the same code at another layout;
  differ   neither: either different code (a different compiler) or a routine the REFs did not link.

Prints per segment the counts, then the `layout` and `differ` routines. Never prints image bytes.
"""
import re
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_16

from inventory import Prog

NUM = re.compile(r'0x[0-9a-f]+|\b\d+\b')
MD = Cs(CS_ARCH_X86, CS_MODE_16)


def pattern(ms):
    return re.compile(b''.join(b'.' if b is None else re.escape(bytes([b])) for b in ms), re.DOTALL)


def shape(img, lin, off, n):
    """the instruction sequence with every number replaced: (mnemonic, operands, size) per instruction"""
    out, used = [], 0
    for ins in MD.disasm(img[lin:lin + n + 16], off):
        if used >= n:
            break
        out.append((ins.mnemonic, NUM.sub('#', ins.op_str), ins.size))
        used += ins.size
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    t = Prog(argv[1])
    refs = [Prog(p) for p in argv[2].split(',')]
    want = {int(s, 16) for s in argv[3:]} or set(t.code)
    hays = [r.img[:r.data_seg * 16] for r in refs]
    for seg in t.code:
        if seg not in want:
            continue
        spans = t.spans(seg)
        count = {'same': [0, 0], 'layout': [0, 0], 'differ': [0, 0]}
        lists = {'layout': [], 'differ': []}
        for off, size in spans:
            n = min(size, t.routines[(seg, off)])
            kind = 'differ'
            if n and any(pattern(t.masked(seg, off, n)).search(h) for h in hays):
                kind = 'same'
            elif n:
                want_shape = shape(t.img, seg * 16 + off, off, n)
                first = t.img[seg * 16 + off]
                for h in hays:
                    k = h.find(bytes([first]))
                    while k != -1 and kind == 'differ':
                        if shape(h, k, off, n) == want_shape:
                            kind = 'layout'
                        k = h.find(bytes([first]), k + 1)
                    if kind != 'differ':
                        break
            count[kind][0] += 1
            count[kind][1] += size
            if kind in lists:
                lists[kind].append('%04X:%04X(%d)' % (seg, off, size))
        print('%04X: %d routines / %d bytes: same %d/%d, layout %d/%d, differ %d/%d' % (
            seg, len(spans), sum(s for _, s in spans), *count['same'], *count['layout'], *count['differ']))
        for kind in ('layout', 'differ'):
            if lists[kind]:
                print('   %-6s %s' % (kind, ' '.join(lists[kind])))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

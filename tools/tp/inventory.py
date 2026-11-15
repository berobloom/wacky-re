#!/usr/bin/env python3
"""Function inventory of a Turbo Pascal MZ: segments from the relocations, routines by recursive descent.

usage: inventory.py UNPACKED.EXE OUT.json

Segments: every relocated word holds a segment value; the distinct values are the segment bases (TP gives each unit
one code segment, the main program first, System last, then the data segment). A segment's size runs to the next base.

Routines: seeds are the program entry, every far call `9A off seg` whose seg word is relocated, and every relocated
seg word whose preceding word is an in-range offset (procedure pointers, `@Proc`). Each seed is decoded by recursive
descent (capstone, 16-bit); near calls found on the way become routines too. A routine's size is entry to the next
entry in the same segment (as fnv_decomp's function_spans.py), so string literals TP stores in front of a routine
fall into the routine above it; `reached` counts the bytes the descent decoded, the rest is `gap`.
`frame_only` lists `55 89 E5` (push bp / mov bp,sp) starts no seed reached: candidates, not routines.

Writes OUT.json; prints one line per segment. Never prints image bytes. `scan()` is used by the other tp tools.
"""
import json
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from capstone.x86 import X86_OP_MEM

BRANCH = {'jmp', 'je', 'jne', 'jb', 'jae', 'jbe', 'ja', 'jl', 'jge', 'jle', 'jg', 'js', 'jns', 'jo', 'jno',
          'jp', 'jnp', 'jcxz', 'loop', 'loope', 'loopne'}
END = {'ret', 'retf', 'iret', 'jmp'}


def load(path):
    d = open(path, 'rb').read()
    if d[:2] not in (b'MZ', b'ZM'):
        raise SystemExit('not an MZ executable: ' + path)
    last, pages, nrel, hdr = struct.unpack_from('<HHHH', d, 2)
    relo, = struct.unpack_from('<H', d, 0x18)
    ip, cs = struct.unpack_from('<HH', d, 0x14)
    end = pages * 512 - (512 - last if last else 0)
    img = d[hdr * 16:end]
    rel = set()
    for i in range(nrel):
        off, seg = struct.unpack_from('<HH', d, relo + 4 * i)
        rel.add(seg * 16 + off)
    return img, rel, (cs, ip)


class Prog:
    """img, rel (linear positions of relocated words), code (segment bases), ends, data_seg, entry,
    routines {(seg, off): reached bytes}, masks {(seg, off): set of linear positions whose bytes depend on layout: call operands, relocated
    words, absolute memory displacements}"""

    def __init__(self, path):
        self.img, self.rel, self.entry = load(path)
        img, rel = self.img, self.rel
        bases = sorted({struct.unpack_from('<H', img, r)[0] for r in rel} | {self.entry[0]})
        bases = [b for b in bases if b * 16 < len(img)]
        self.data_seg = bases[-1]
        self.code = bases[:-1]
        self.ends = {b: (self.code[i + 1] * 16 if i + 1 < len(self.code) else self.data_seg * 16)
                     for i, b in enumerate(self.code)}
        self._descend()

    def in_code(self, seg, off):
        return seg in self.ends and seg * 16 + off < self.ends[seg]

    def _descend(self):
        img, rel = self.img, self.rel
        seeds = {self.entry}
        for r in rel:
            seg, = struct.unpack_from('<H', img, r)
            if r >= 2 and seg in self.ends:
                off, = struct.unpack_from('<H', img, r - 2)
                if self.in_code(seg, off) and (off or (r >= 3 and img[r - 3] == 0x9A)):
                    seeds.add((seg, off))
        md = Cs(CS_ARCH_X86, CS_MODE_16)
        md.detail = True
        self.routines, self.masks = {}, {}
        work = list(seeds)
        while work:
            seg, off = work.pop()
            if (seg, off) in self.routines or not self.in_code(seg, off):
                continue
            todo, seen, nbytes, mask = [off], set(), 0, set()
            self.routines[(seg, off)] = 0
            while todo:
                pc = todo.pop()
                while self.in_code(seg, pc) and pc not in seen:
                    lin = seg * 16 + pc
                    ins = next(md.disasm(img[lin:lin + 8], pc), None)
                    if ins is None:
                        break
                    seen.add(pc)
                    nbytes += ins.size
                    m, op = ins.mnemonic, ins.op_str
                    if m == 'call' and op.startswith('0x') and img[lin] == 0xE8:
                        work.append((seg, int(op, 16)))
                        mask.update((lin + 1, lin + 2))
                    elif m == 'lcall' and img[lin] == 0x9A and lin + 3 in rel:
                        t_off, t_seg = struct.unpack_from('<HH', img, lin + 1)
                        work.append((t_seg, t_off))
                        mask.update((lin + 1, lin + 2))
                    if ins.disp_size and any(o.type == X86_OP_MEM and not o.mem.base and not o.mem.index
                                             for o in ins.operands):
                        mask.update(range(lin + ins.disp_offset, lin + ins.disp_offset + ins.disp_size))
                    for k in range(lin, lin + ins.size - 1):
                        if k in rel:
                            mask.update((k, k + 1))
                    if m in BRANCH and op.startswith('0x'):
                        todo.append(int(op, 16))
                    if m in END:
                        break
                    pc = (pc + ins.size) & 0xFFFF
            self.routines[(seg, off)] = nbytes
            self.masks[(seg, off)] = mask

    def spans(self, seg):
        offs = sorted(o for s, o in self.routines if s == seg)
        size = self.ends[seg] - seg * 16
        return [(o, (offs[i + 1] if i + 1 < len(offs) else size) - o) for i, o in enumerate(offs)]

    def masked(self, seg, off, size):
        """the routine's bytes with layout-dependent ones (relocated words, call targets, absolute memory operands:
        DS offsets of variables and CS offsets of in-code constants) as None"""
        lin = seg * 16 + off
        mask = self.masks.get((seg, off), set())
        rel = self.rel
        out = []
        for k in range(lin, lin + size):
            out.append(None if k in mask or k in rel or (k - 1) in rel else self.img[k])
        return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    p = Prog(argv[1])
    img = p.img
    frame_only = []
    for seg in p.code:
        lo, hi = seg * 16, p.ends[seg]
        i = img.find(b'\x55\x89\xe5', lo, hi)
        while i != -1:
            if (seg, i - lo) not in p.routines:
                frame_only.append('%04X:%04X' % (seg, i - lo))
            i = img.find(b'\x55\x89\xe5', i + 1, hi)
    units = []
    for n, seg in enumerate(p.code):
        size = p.ends[seg] - seg * 16
        fns = [{'addr': '%04X:%04X' % (seg, o), 'size': sz, 'reached': p.routines[(seg, o)],
                'name': None, 'src': None, 'status': 'unmatched'} for o, sz in p.spans(seg)]
        head = int(fns[0]['addr'][5:], 16) if fns else size
        units.append({'seg': '%04X' % seg, 'index': n, 'image_offset': seg * 16, 'size': size,
                      'unit': None, 'compiler': None, 'lead_bytes': head, 'functions': fns})
        print('CODE_%-2d %04X  size %6d  routines %4d  bytes before first %d' % (n, seg, size, len(fns), head))
    out = {'image': argv[1], 'entry': '%04X:%04X' % p.entry, 'data_segment': '%04X' % p.data_seg,
           'data_size': len(img) - p.data_seg * 16, 'relocations': len(p.rel), 'units': units,
           'frame_only': frame_only,
           'totals': {'units': len(units), 'functions': sum(len(u['functions']) for u in units),
                      'code_bytes': p.data_seg * 16}}
    json.dump(out, open(argv[2], 'w'), indent=1)
    print('total %d routines in %d code segments; %d frame-only candidates -> %s'
          % (out['totals']['functions'], len(units), len(frame_only), argv[2]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

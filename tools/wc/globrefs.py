#!/usr/bin/env python3
"""globrefs.py: the absolute data addresses each side of a near miss really touches.

usage: globrefs.py PROG FILE.OBJ NAME [0xADDR]

nmrefs labels an indexed access by the array it falls in (its `array_of`), and masks our fixup
bytes, so a store base that is wrong by a few bytes *inside one array* shows up nowhere: iter 59
found fn_173c4 writing the 0x7b238 table at 0x7b230 (sp7b230b) with 100 rows unchanged. This
resolves both sides to linear addresses and prints only the ones one side lacks:

  ours: an OMF ext fixup's address = its symbol's address in .work/link/symmap.txt
        (tools/link/symmap.py) + the addend, which lives in the .OBJ bytes at the fixup location
        (libmatch's fixups_of reports that addend as 0 for loc 9).
  game: the linked image's own operands, from .work/le/PROG.obj1.bin.

Address operands below 0x60000 (code) and our branches/calls are skipped.
"""
import json
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import libmatch as L  # noqa: E402
import objmatch as M  # noqa: E402

ROOT = L.ROOT
LO, HI = 0x60000, 0x90000


def symmap():
    out = {}
    p = os.path.join(ROOT, '.work', 'link', 'symmap.txt')
    for line in open(p):
        f = line.split()
        if len(f) >= 2 and f[1].startswith('0x'):
            out[f[0]] = int(f[1], 16)
    return out


def main():
    prog, obj = sys.argv[1], sys.argv[2]
    name = sys.argv[3]
    addr = int(sys.argv[4], 16) if len(sys.argv) > 4 else M.names(prog)[name]
    meta = json.load(open(os.path.join(ROOT, '.work', 'le', prog + '.json')))
    code = open(os.path.join(ROOT, '.work', 'le', prog + '.obj1.bin'), 'rb').read()
    cbase = meta['objects'][0]['base']
    syms = symmap()
    (_, recs), = list(L.modules(open(obj, 'rb').read()))
    segs = L.code_of(recs)
    pubat = sorted((o, nm) for s, o, nm in M.publics(recs) if s in segs)
    off = next(o for o, nm in pubat if nm.startswith(name))
    nxt = next(o for o, _ in pubat if o > off)
    ours = L.fixups_of(recs)
    data, _ = segs['_TEXT']
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    orefs = {}
    for seg, at, loc, selfrel, kind, nm, _ in ours:
        if seg != '_TEXT' or not (off <= at < nxt) or kind != 'ext' or selfrel:
            continue
        base = syms.get(nm, syms.get('_' + M.plain(nm)))
        if base is None:
            print('  ? %s at our 0x%x has no symmap row' % (nm, at))
            continue
        w = 4 if loc == 9 else 2
        v = base + int.from_bytes(data[at:at + w], 'little', signed=True)
        if LO <= v < HI:
            orefs.setdefault(v, []).append(hex(at))
    for ins in md.disasm(data[off:nxt], off):     # our immediates (a literal record address)
        if ins.group(capstone.CS_GRP_CALL) or not ins.operands:
            continue
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_IMM and LO <= (op.imm & 0xFFFFFFFF) < HI:
                orefs.setdefault(op.imm & 0xFFFFFFFF, []).append(hex(ins.address))
    gend = next((a for a in sorted(M.names(prog).values()) if a > addr), addr + 4096)
    gins = list(md.disasm(code[addr - cbase:gend - cbase], addr))
    for ins in reversed(gins):                    # the next function's head is not part of this one
        if ins.mnemonic == 'ret':
            gins = [i for i in gins if i.address <= ins.address]
            break
    grefs = {}
    for ins in gins:
        for op in ins.operands:
            v = None
            if op.type == capstone.x86.X86_OP_IMM and not ins.group(capstone.CS_GRP_CALL):
                v = op.imm & 0xFFFFFFFF
            elif op.type == capstone.x86.X86_OP_MEM:
                v = op.mem.disp
            if v and LO <= v < HI:
                grefs.setdefault(v, []).append(hex(ins.address))
    print('%s  ours our 0x%x-0x%x  game 0x%x-0x%x' % (name, off, nxt, addr, gins[-1].address))
    for side, a, b in (('ours only', orefs, grefs), ('game only', grefs, orefs)):
        for v in sorted(set(a) - set(b)):
            print('    %s 0x%x  x%d  %s' % (side, v, len(a[v]), ' '.join(a[v][:6])))
    for v in sorted(set(orefs) & set(grefs)):
        if len(orefs[v]) != len(grefs[v]):
            print('    counts 0x%x  ours x%d  game x%d  %s / %s' %
                  (v, len(orefs[v]), len(grefs[v]), ' '.join(orefs[v][:4]), ' '.join(grefs[v][:4])))


if __name__ == '__main__':
    main()

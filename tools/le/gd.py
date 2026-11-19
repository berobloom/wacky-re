#!/usr/bin/env python3
"""Chunked whole-code disassembly of a LE program, for grep only.
usage: gd.py <prog> <start> <end> > out.txt"""
import os, re, sys
import capstone
from lelib import Image, ROOT

def load_names(prog):
    p = os.path.join(ROOT, 'analysis', prog + '_names.txt')
    names = {}
    if os.path.exists(p):
        for line in open(p):
            line = line.split('#')[0].split()
            if len(line) >= 2:
                names[int(line[0], 16)] = line[1]
    return names

def annotate(img, names, op_str):
    notes = []
    for m in re.finditer(r'0x[0-9a-f]+', op_str):
        v = int(m.group(0), 16)
        if v in names:
            notes.append(names[v])
        elif v >= img.data_base:
            s = img.cstr(v)
            if s:
                notes.append(repr(s[:40]))
    return ('  ; ' + ' '.join(notes)) if notes else ''

prog, start, end = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
img = Image(prog)
names = load_names(prog)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
code = img.read(start, end - start)
addr = start
i = 0
while i < len(code):
    chunk = code[i:min(i + 4096, len(code))]
    insl = list(md.disasm(chunk, addr + i))
    if not insl:
        i += 1
        continue
    for ins in insl:
        print('%08x  %-22s%s %s%s' % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str,
                                      annotate(img, names, ins.op_str)))
    i = insl[-1].address + insl[-1].size - addr

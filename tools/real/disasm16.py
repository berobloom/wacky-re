#!/usr/bin/env python3
"""Disassemble real-mode code from a RAM dump or a save state (capstone 16-bit).

usage: disasm16.py DUMP CS_SEG START [END | +COUNT]
  DUMP    a linear RAM dump starting at linear 0 (dump_memory 0,0,655360),
          or a DOSBox-X .state (its Memory member is read)
  CS_SEG  the code segment of THIS boot (hex), e.g. 084A
  START   CS offset (hex); END a CS offset, or +COUNT instructions (default +40)

Branch and call targets are printed as CS offsets (CS+xxxx). capstone masks
16-bit targets to 0xFFFF and gdb's real-mode disassembly truncates, so never
read a target off either without this correction. A call rel16 (E8 lo hi) is
3 bytes: target = (site + 3 + rel16) & 0xFFFF.
"""
import sys
import zipfile

from capstone import CS_ARCH_X86, CS_MODE_16, Cs

BRANCH = ('j', 'call', 'loop')


def load(path):
    if path.endswith('.state'):
        return zipfile.ZipFile(path).read('Memory')[136:]
    return open(path, 'rb').read()


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    mem = load(argv[1])
    base = int(argv[2], 16) * 16
    start = int(argv[3], 16)
    end, count = 0x10000, 40
    if len(argv) > 4:
        if argv[4].startswith('+'):
            count = int(argv[4][1:])
        else:
            end, count = int(argv[4], 16), 1 << 30
    code = mem[base:base + 0x10000]
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    # capstone stops silently at the first invalid instruction; print such
    # bytes as `.byte` and go on (a wrong START often lands mid-instruction)
    md.skipdata = True
    n = 0
    for i in md.disasm(code[start:end], start):
        s = i.op_str
        if i.mnemonic.startswith(BRANCH) and '[' not in s and ' ' not in s:
            try:
                s = 'CS+%04X' % (int(s, 16) & 0xFFFF)
            except ValueError:
                pass
        print('CS+%04X  %05X  %-14s %-7s %s' % (
            i.address, base + i.address, i.bytes.hex(), i.mnemonic, s))
        n += 1
        if n >= count:
            break
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

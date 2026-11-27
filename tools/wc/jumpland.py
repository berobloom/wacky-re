#!/usr/bin/env python3
"""jumpland.py: near-miss jumps that land somewhere else in the game - the missed `continue`,
`break` and `goto` statements.

usage: jumpland.py PROG FILE.OBJ [NAME ...]      (no names: every function of src/nearmiss.txt
                                                  whose status is not MATCH)

Aligns our function with the game's as tools/wc/fndiff.py does (instruction text, addresses as @).
For every jump that sits in an aligned run on both sides (jmp/jcc, same mnemonic) it follows both
targets: if our target, carried through the alignment, is not the game's target, the pair is
printed with what each side lands on. Where the game's jump lands names the statement
(GOTCHAS "Where a jump inside a loop lands"): the loop's bottom test = `continue`, the top of a
loop body past its test = `goto` to a label there, past the loop = `break`, the shared tail of two
arms = a merged/duplicated statement.

A jump whose target falls in a non-aligned run on either side is reported as `unaligned` - a
moved block, not necessarily a statement. Output per function: `NAME: N jumps, K land elsewhere`.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import json                          # noqa: E402
import capstone                      # noqa: E402
import libmatch as L                 # noqa: E402
import objmatch as M                 # noqa: E402
import fndiff as F                   # noqa: E402

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def listing(buf, vma):
    out = []
    for i in MD.disasm(buf, vma):
        tgt = None
        if (i.mnemonic == 'jmp' or i.mnemonic.startswith('j')) and re.fullmatch(r'0x[0-9a-f]+', i.op_str):
            tgt = int(i.op_str, 16)
        out.append((i.address, F.norm(i.mnemonic + ' ' + i.op_str), tgt, i.mnemonic + ' ' + i.op_str))
    return out


def scan(name, A, B, verbose=True):
    a_idx = {x[0]: k for k, x in enumerate(A)}
    b_idx = {x[0]: k for k, x in enumerate(B)}
    amap = {}
    for tag, i1, i2, j1, j2 in F.align([x[1] for x in A], [x[1] for x in B]):
        if tag == 'equal':
            for d in range(i2 - i1):
                amap[i1 + d] = j1 + d
    jumps = bad = 0
    for i, j in sorted(amap.items()):
        ta, tb = A[i][2], B[j][2]
        if ta is None or tb is None:
            continue
        jumps += 1
        ka, kb = a_idx.get(ta), b_idx.get(tb)
        if ka is None or kb is None:
            continue                          # leaves the function (shared epilogue, tail call)
        if amap.get(ka) == kb:
            continue
        bad += 1
        if verbose:
            mapped = amap.get(ka)
            print('  %s %-16s ours -> %#x %-34s | game -> %#x %s%s' % (
                hex(B[j][0]), A[i][3].split()[0], ta, A[ka][3], tb, B[kb][3],
                '' if mapped is not None else '   (ours lands in an unaligned run)'))
    print('%s: %d jumps, %d land elsewhere' % (name, jumps, bad))
    return bad


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    prog, obj = sys.argv[1:3]
    want = sys.argv[3:]
    if not want:
        root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        for l in open(os.path.join(root, 'src', 'nearmiss.txt')):
            m = re.match(r'\d+\s+0x[0-9a-f]+\s+(\w+)\s*(.*)', l)
            if m and not m.group(2).startswith('MATCH'):
                want.append(m.group(1))
    meta = json.load(open(os.path.join(L.ROOT, '.work', 'le', prog + '.json')))
    code = open(os.path.join(L.ROOT, '.work', 'le', prog + '.obj1.bin'), 'rb').read()
    cbase = meta['objects'][0]['base']
    names = M.names(prog)
    (_, recs), = list(L.modules(open(obj, 'rb').read()))
    segs = L.code_of(recs)
    pubs = sorted(p for p in M.publics(recs) if p[0] in segs)
    for name in want:
        hit = [p for p in pubs if M.plain(p[2]) == M.plain(name)]
        addr = names.get(M.plain(name), 0)
        if not hit or not addr:
            print('%s: not in obj or not named' % name)
            continue
        seg, off, _ = hit[0]
        end = next((q[1] for q in pubs if q[0] == seg and q[1] > off), len(segs[seg][0]))
        buf, mask = segs[seg]
        ours = bytes(b if not mask[off + k] else 0x90 for k, b in enumerate(buf[off:end]))
        A = listing(ours, addr)
        B = listing(code[addr - cbase:addr - cbase + len(ours) + 16], addr)
        B = [x for x in B if x[0] < addr + len(ours)]
        scan(name, A, B)


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""stackmap.py: what a function does with its stack frame - the slots it touches and the holes it
leaves - for our compiled copy and for the game's, side by side.

usage: stackmap.py FILE.OBJ PROG NAME [NAME...] [-v]

A near miss whose rows are only stack displacements (two slot numbers swapped, or one slot moved) is
a frame-shape question, not a spelling question: WCC puts the 4-byte scalars in a band at the top of
the frame, then a gap, then the things it will not put in that band - local arrays and 8-byte
objects. Unused locals, dead stores, propagated constants and `register` cost no frame space (found
on redcat-rrr-re with WCC 10.5, cos_sin_init 0x13c34; GOTCHAS.md says what holds for 9.5b here).

Prints per public an `ours` line and, when the name is in analysis/PROG_names.txt, a `game` line:
frame size, the 4-byte slots touched (high to low) and the holes below the top one. The game's
function is read for as many bytes as ours has, so a size difference cuts or overruns its tail.
With -v, also every instruction that touches each slot. Fixup bytes are masked as in fndiff, so a
`mov eax,@` that loads a global is not mistaken for a frame access.
"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capstone
import libmatch as L
import objmatch as M

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

def dis(buf, vma):
    return [i.mnemonic + ' ' + i.op_str for i in MD.disasm(buf, vma)]


SLOT = re.compile(r'\[esp(?: \+ (0x[0-9a-f]+|[0-9]+))?\]')      # capstone: [esp], [esp + 8], [esp + 0x1c]


def frame_map(txt):
    frames = [int(m.group(1), 0) for t in txt for m in [re.match(r'sub esp, (0x[0-9a-f]+|[0-9]+)$', t)] if m]
    used = {}
    for t in txt:
        for m in SLOT.finditer(t):
            used.setdefault(int(m.group(1) or '0', 0) & ~3, []).append(t)
    return (frames[0] if frames else None), used


def show(tag, name, txt, verbose):
    frame, used = frame_map(txt)
    fr = hex(frame) if frame is not None else '-'
    if not used:
        print('%-5s %-22s frame=%s  no esp-relative slots' % (tag, name, fr))
        return
    holes = [s for s in range(max(used) - 4, -1, -4) if s not in used]
    runs = []
    for s in holes:
        if runs and s == runs[-1][1] - 4:
            runs[-1][1] = s
        else:
            runs.append([s, s])
    print('%-5s %-22s frame=%s  slots: %s  holes: %s' %
          (tag, name, fr, ' '.join('%x' % s for s in sorted(used, reverse=True)),
           ' '.join('%x-%x' % (a, b) for a, b in runs) or '-'))
    if verbose:
        for s in sorted(used, reverse=True):
            for t in used[s]:
                print('        [esp+%-4x] %s' % (s, t))


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    obj, prog = sys.argv[1], sys.argv[2]
    want = [n for n in sys.argv[3:] if not n.startswith('-')]
    verbose = '-v' in sys.argv
    (_, recs), = list(L.modules(open(obj, 'rb').read()))
    segs = L.code_of(recs)
    pubs = sorted(p for p in M.publics(recs) if p[0] in segs)
    code = cbase = None
    le = os.path.join(L.ROOT, '.work', 'le', prog)
    if os.path.exists(le + '.json'):
        cbase = json.load(open(le + '.json'))['objects'][0]['base']
        code = open(le + '.obj1.bin', 'rb').read()
    names = M.names(prog) if code else {}
    for name in want:
        hit = [p for p in pubs if p[2] == name or M.plain(p[2]) == M.plain(name)]
        if not hit:
            print('%-28s not in %s' % (name, os.path.basename(obj)))
            continue
        seg, off, pname = hit[0]
        end = next((q[1] for q in pubs if q[0] == seg and q[1] > off), len(segs[seg][0]))
        buf, mask = segs[seg]
        ours = bytes(b if not mask[off + k] else 0x90 for k, b in enumerate(buf[off:end]))
        show('ours', pname, dis(ours, 0x1000), verbose)
        addr = names.get(M.plain(pname), 0)
        if addr:
            game = code[addr - cbase:addr - cbase + len(ours)]
            show('game', hex(addr), dis(game, addr), verbose)


if __name__ == '__main__':
    sys.exit(main())

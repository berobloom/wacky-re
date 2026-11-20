#!/usr/bin/env python3
"""genasm.py: bootstrap nasm source for WW.EXE's assembler module 0x36A4E-0x409EE.

usage: tools/asm/genasm.py [OUT]          (default src/wwasm.asm; needs .work/le/ww.* and
                                           .work/link/symmap.txt from tools/link/build.sh)

One source line per original instruction, so the source can be checked line by line against the
game (tools/asm/asmcheck.py). Symbols:
  - every LE fixup in the range becomes a name: the symmap name at that exact address, else D_xxxxx;
    declared at the top with EXT name, addr (BIN build: `equ addr`; OBJ build: `extern`);
  - code labels: symmap names (the C side's `name_`, all aliases) at entry points, L_xxxxx at branch
    targets;
  - branches keep the original's short/near form.
An instruction nasm does not take, or encodes differently from the original, is emitted as `db`
with the instruction in the comment; the loop reassembles until every line matches.
Run once to bootstrap; after that src/wwasm.asm is the source and this script is history (its one
hand edit: the sel16 `mov bx, seg _v88b30` at 0x4090c, which this script leaves as db)."""
import collections
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'link'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
from lefix import fixups               # noqa: E402
from lelib import Image                # noqa: E402
import capstone                        # noqa: E402

LO, HI = 0x36A4E, 0x409EF
BRANCH = re.compile(r'^(j\w+|call|loop\w*|jecxz|jcxz)$')


def symmap():
    names = collections.defaultdict(list)
    for l in open(os.path.join(ROOT, '.work', 'link', 'symmap.txt')):
        p = l.split()
        if len(p) >= 2 and p[1].startswith('0x'):
            if p[0] not in names[int(p[1], 16)]:
                names[int(p[1], 16)].append(p[0])
    return names


def to_nasm(i):
    """capstone Intel text -> nasm text (None if not handled)."""
    m, o = i.mnemonic, i.op_str
    m = {'pushal': 'pushad', 'popal': 'popad', 'pushfl': 'pushfd', 'popfl': 'popfd'}.get(m, m)
    if re.match(r'^(rep\w* )?(movs|stos|lods|scas|cmps|ins|outs)[bwd]$', m):
        segs = re.findall(r'\b([cdefgs]s):\[', o)
        if any(s not in ('es', 'ds') for s in segs) or ('es:[esi]' in o):
            return None                    # a segment override: db
        return m
    o = re.sub(r'\b(byte|word|dword|qword|tbyte|xword) ptr ', lambda x: {'xword': 'tword'}.get(x[1], x[1]) + ' ', o)
    o = re.sub(r'\b([cdefgs]s):\[', r'[\1:', o)
    o = re.sub(r'\bst\((\d)\)', r'st\1', o)
    return (m + ' ' + o).strip()


REGS = [('eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'),
        ('ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di'),
        ('al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh')]
RROPS = {0x02: 'add', 0x0a: 'or', 0x12: 'adc', 0x1a: 'sbb', 0x22: 'and', 0x2a: 'sub', 0x32: 'xor',
         0x3a: 'cmp', 0x8a: 'mov'}
ACC = {0x05: 'add', 0x0d: 'or', 0x15: 'adc', 0x1d: 'sbb', 0x25: 'and', 0x2d: 'sub', 0x35: 'xor', 0x3d: 'cmp'}
MACROS = ['; rr op, dst, src: a register-register op in the MASM/TASM form (opcode direction bit set,',
          '; 8B C8 for mov ecx,eax), which nasm never picks; the original assembler always did.',
          '%macro REG 4',
          '%define RN_%1 %2',
          '%define RW_%1 %3',
          '%define RP_%1 %4',
          '%endmacro']
for _w, _rs in ((1, REGS[0]), (1, REGS[1]), (0, REGS[2])):
    for _n, _r in enumerate(_rs):
        MACROS.append('REG %s, %d, %d, %d' % (_r, _n, _w, _rs is REGS[1]))
MACROS += ['%%define OP_%s 0x%02x' % (v, k) for k, v in sorted(RROPS.items())]
MACROS += ['%macro rr 3',
           '%if RP_%2',
           '        db 0x66',
           '%endif',
           '        db OP_%1 | RW_%2, 0xc0 | (RN_%2 << 3) | RN_%3',
           '%endmacro']


def alt(i, cur):
    """The next spelling to try for an instruction nasm encoded differently."""
    b = bytes(i.bytes)
    p16 = b[:1] == b'\x66'
    op = b[1:] if p16 else b
    if not cur.startswith(('rr ', 'db ')) and 'strict' not in cur:
        if len(op) == 2 and op[0] & 0xfe in RROPS and op[0] & 1 == (op[0] & 1) and op[1] >> 6 == 3:
            w = op[0] & 1
            regs = REGS[2] if not w else REGS[1] if p16 else REGS[0]
            return 'rr %s, %s, %s' % (RROPS[op[0] & 0xfe], regs[(op[1] >> 3) & 7], regs[op[1] & 7])
        if op[:1] and op[0] in ACC:
            m = re.match(r'^(\w+) (\w+), (.+)$', cur)
            if m:
                return '%s %s, strict %s %s' % (m[1], m[2], 'word' if p16 else 'dword', m[3])
    return 'db ' + ','.join('0x%02x' % x for x in b) + ' ; ' + (i.mnemonic + ' ' + i.op_str).strip()


def main(argv):
    out = argv[0] if argv else os.path.join(ROOT, 'src', 'wwasm.asm')
    img = Image('ww')
    le, fx = fixups(os.path.join(ROOT, 'original', 'WW.EXE'))
    code = img.read(LO, HI - LO)
    names = symmap()
    fix = {s: (k, t) for s, k, t in fx if LO <= s < HI}
    from_outside = {t for s, k, t in fx if LO <= t < HI and not LO <= s < HI}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = False
    ins = []
    pc = LO
    while pc < HI:
        i = next(md.disasm(code[pc - LO:pc - LO + 16], pc))
        ins.append(i)
        pc += i.size
    starts = {i.address for i in ins}
    labels = set(from_outside)
    for a in names:
        if LO <= a < HI:
            labels.add(a)
    for i in ins:
        if BRANCH.match(i.mnemonic):
            try:
                labels.add(int(i.op_str, 16))
            except ValueError:
                pass
    bad = sorted(a for a in labels if a not in starts)
    if bad:
        print('labels inside an instruction:', ' '.join(hex(a) for a in bad))

    def lname(a):
        return [n for n in names.get(a, []) if re.match(r'^[A-Za-z_]\w*$', n)] or ['L_%05x' % a]

    ext = {}

    def dname(t):
        nn = [n for n in names.get(t, []) if re.match(r'^[A-Za-z_]\w*$', n)]
        n = nn[0] if nn else 'D_%05x' % t
        ext[n] = t
        return n

    lines = {}                              # address -> source text
    for i in ins:
        raw = bytes(i.bytes)
        db = 'db ' + ','.join('0x%02x' % b for b in raw) + ' ; ' + (i.mnemonic + ' ' + i.op_str).strip()
        sites = [q for q in range(i.address, i.address + i.size) if q in fix]
        txt = to_nasm(i)
        if txt is None:
            lines[i.address] = db
            continue
        if BRANCH.match(i.mnemonic):
            try:
                t = int(i.op_str, 16)
            except ValueError:
                lines[i.address] = txt
                continue
            m = i.mnemonic
            form = '' if m == 'call' or m.startswith('loop') or m in ('jecxz', 'jcxz') else \
                ('short ' if i.size == 2 else 'near ')
            lines[i.address] = '%s %s%s' % (m, form, lname(t)[0])
            continue
        ok = True
        for q in sites:
            k, t = fix[q]
            if k == 'sel16':
                ok = False                  # `mov bx, seg x`: hand-written below
                break
            h = '0x%x' % t
            if txt.count(h) != 1:
                ok = False
                break
            txt = re.sub(re.escape(h) + r'\b', dname(t), txt)
        lines[i.address] = txt if ok else db

    def render(path, bin_mode=True):
        w = []
        amap = {}
        w.append('; WW.EXE assembler module 0x36A4E-0x409EE (nasm). Generated by tools/asm/genasm.py,')
        w.append('; checked by tools/asm/asmcheck.py: assembled with -DBIN it is the game\'s bytes exactly.')
        w.append('')
        w.append('%ifdef BIN')
        w.append('%macro EXT 2')
        w.append('%1 equ %2')
        w.append('%endmacro')
        w.append('        org 0x36a4e')
        w.append('%else')
        w.append('%macro EXT 2')
        w.append('        extern %1')
        w.append('%endmacro')
        w.append('        segment WWASM_TEXT byte public use32 class=CODE FLAT')
        w.append('%endif')
        w.append('        bits 32')
        w.append('')
        w.extend(MACROS)
        w.append('')
        for n, t in sorted(ext.items(), key=lambda x: x[1]):
            w.append('        EXT %s, 0x%05x' % (n, t))
        w.append('')
        for a in sorted(labels):
            for n in lname(a):
                if not n.startswith('L_'):
                    w.append('        global %s' % n)
        w.append('')
        for i in ins:
            a = i.address
            if a in labels:
                for n in lname(a):
                    w.append('%s:' % n)
            w.append('        %s' % lines[a])
            amap[len(w)] = a
        open(path, 'w').write('\n'.join(w) + '\n')
        return amap

    byaddr = {x.address: x for x in ins}
    # assemble, compare line by line, turn every differing line into db; repeat
    for rnd in range(10):
        tmp = tempfile.mkdtemp()
        src = os.path.join(tmp, 'a.asm')
        addr_of_line = render(src)
        lst = os.path.join(tmp, 'a.lst')
        r = subprocess.run(['nasm', '-DBIN', '-f', 'bin', '-o', os.path.join(tmp, 'a.bin'), '-l', lst, src],
                           capture_output=True, text=True)
        errlines = set()
        for l in r.stderr.splitlines():
            m = re.match(r'.*?:(\d+): error', l)
            if m:
                errlines.add(int(m[1]))
        srclines = open(src).read().split('\n')
        if errlines:
            for n in errlines:
                a = addr_of_line.get(n)
                if a is None:
                    print('error outside instructions, line', n, srclines[n - 1])
                    print(r.stderr[:2000])
                    return 1
                i = byaddr[a]
                lines[a] = 'db ' + ','.join('0x%02x' % b for b in i.bytes) + ' ; ' + (i.mnemonic + ' ' + i.op_str).strip()
            print('round %d: %d lines nasm refused -> db' % (rnd, len(errlines)))
            continue
        # listing: bytes per source line
        got = collections.defaultdict(bytearray)
        top = None                          # macro-expansion lines (marked <n>) carry the
        for l in open(lst):                 # macro's line numbers: credit their bytes to the caller
            m = re.match(r'^\s*(\d+)\s+(?:([0-9A-F]{8})\s+([0-9A-F()\[\]]+)-?)?\s*(?:<(\d+)>)?', l)
            if not m:
                continue
            deep = m[4] is not None
            if not deep:
                top = int(m[1])
            if m[3] and top is not None:
                got[top] += bytes.fromhex(re.sub(r'[()\[\]]', '', m[3]))
        changed = 0
        for n, a in addr_of_line.items():
            i = byaddr[a]
            want = bytes(i.bytes)
            g = bytes(got.get(n, b''))
            if g != want and not lines[a].startswith('db '):
                lines[a] = alt(i, lines[a])
                changed += 1
        print('round %d: %d lines encode differently -> next spelling' % (rnd, changed))
        if not changed:
            break
    render(out)
    nd = sum(1 for v in lines.values() if v.startswith('db '))
    print('%s: %d instructions, %d as db, %d labels, %d data names' % (out, len(ins), nd, len(labels), len(ext)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""progress.py: writes PROGRESS.md, how much of WW.EXE is rebuilt from C.

usage: tools/progress.py [--out FILE]

Builds src/ww.c with src/check.sh and compares it the same way: a function counts as done only when
our compiled bytes equal the game's (fixup bytes masked, fixup targets checked). Near misses get
their remaining instruction rows from tools/wc/fndiff.py. Functions of the game's C module that
src/ww.c does not have yet come from tools/le/funcs.py's list (.work/le/ww_funcs.tsv), named from
analysis/ww_names.txt. Subsystems come from analysis/ww_subsystems.txt. The library rows come from
tools/wc/libmatch.py against Watcom 9.5b's CLIB3R.LIB.

The sound library rows come from tools/ass/asscheck.py (the src/ass port, rebuilt and compared) and the
names file's released-LIB matches; the summary line counts the whole code object.

It also writes docs/WWPROGRESS.HTML (every function, searchable and filterable, with its instruction
diff against the game; laid out like reccmp's reports for isledecomp/isle, template tools/progress.html)
and docs/WWPROGRESS.SVG (the progress badge the README shows).
"""
import base64
import difflib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
PY = os.path.join(ROOT, '.work', 'venv', 'bin', 'python')
CLIB = os.path.join(ROOT, '.work', 'toolchains', 'inst', 'WC95B', 'LIB386', 'DOS', 'CLIB3R.LIB')
MATHLIB = os.path.join(ROOT, '.work', 'toolchains', 'inst', 'WC95B', 'LIB386', 'MATH387R.LIB')

# WW.EXE's code object, findings/executables.md "Code layout (obj1)"
C_START, ASM_START, LIB_START, CODE_END = 0x10260, 0x36A4E, 0x409EF, 0x10000 + 0x4BE2E


def run(args, **kw):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True, **kw).stdout


def names_file():
    out = {}
    for line in open(os.path.join(ROOT, 'analysis', 'ww_names.txt')):
        f = line.split('#')[0].split()
        if len(f) >= 2 and f[0].startswith('0x'):
            out.setdefault(int(f[0], 16), f[1])
    return out


def subsystems():
    order, of = [], {}
    cur = None
    for line in open(os.path.join(ROOT, 'analysis', 'ww_subsystems.txt')):
        line = line.strip()
        if line.startswith('## '):
            cur = line[3:].strip()
            order.append(cur)
            continue
        line = line.split('#')[0].strip()
        if line and cur:
            for n in line.split():
                of[n] = cur
    return order, of


def align_all(near):
    """For each near miss {addr: name}: our instructions and the game's aligned as fndiff.py does,
    the count of differing rows, the similarity ratio and diff hunks with 3 rows of context."""
    import fndiff as F
    import libmatch as L
    import objmatch as M
    meta = json.load(open(os.path.join(ROOT, '.work', 'le', 'ww.json')))
    code = open(os.path.join(ROOT, '.work', 'le', 'ww.obj1.bin'), 'rb').read()
    cbase = meta['objects'][0]['base']
    (_, recs), = list(L.modules(open(os.path.join(ROOT, '.work', 'src', 'WW.OBJ'), 'rb').read()))
    segs = L.code_of(recs)
    pubs = sorted(p for p in M.publics(recs) if p[0] in segs)
    out = {}
    for addr, name in near.items():
        hit = [p for p in pubs if p[2] == name or M.plain(p[2]) == name]
        if not hit:
            continue
        seg, off, _ = hit[0]
        end = next((q[1] for q in pubs if q[0] == seg and q[1] > off), len(segs[seg][0]))
        buf, mask = segs[seg]
        ours = bytes(b if not mask[off + k] else 0x90 for k, b in enumerate(buf[off:end]))
        A = F.dis(ours, addr, addr + len(ours))
        B = F.dis(code[addr - cbase:addr - cbase + len(ours)], addr, addr + len(ours))
        ops = F.align([t[2] for t in B], [t[2] for t in A])
        d = F.rows(ops)
        eq = sum(i2 - i1 for t, i1, i2, j1, j2 in ops if t == 'equal')
        sm = difflib.SequenceMatcher(None, [], [])
        sm.get_opcodes = lambda: ops
        hunks = []
        for grp in sm.get_grouped_opcodes(3):
            h = []
            for t, i1, i2, j1, j2 in grp:
                if t == 'equal':
                    h += [[' ', B[i][0], A[j][0], B[i][2]] for i, j in zip(range(i1, i2), range(j1, j2))]
                else:
                    h += [['-', B[i][0], 0, B[i][2]] for i in range(i1, i2)]
                    h += [['+', 0, A[j][0], A[j][2]] for j in range(j1, j2)]
            hunks.append(h)
        out[addr] = {'d': d, 'i': eq + d, 'r': round(2.0 * eq / (len(A) + len(B)), 4), 'h': hunks}
    return out


def logo_png():
    """The Wacky Wheels logo cut from the main-menu capture (docs/media/menu.jpg), as PNG."""
    import io
    from PIL import Image
    buf = io.BytesIO()
    Image.open(os.path.join(ROOT, 'docs', 'media', 'menu.jpg')).convert('RGB').crop((180, 0, 470, 145)).save(buf, 'PNG')
    return base64.b64encode(buf.getvalue()).decode()


def write_html(path, allf, sub_of, order, al, head):
    fs = []
    for a, n, s in allf:
        f = {'a': a, 'n': n, 's': s, 'sub': sub_of.get(n, 'unsorted')}
        if s == 'near' and a in al:
            f.update(al[a])
        elif s == 'near':
            f.update({'d': 0, 'i': 0, 'r': 0.0, 'h': []})
        fs.append(f)
    subs = [g for g in order if any(f['sub'] == g for f in fs)]
    subs += ['unsorted'] if any(f['sub'] == 'unsorted' for f in fs) else []
    data = json.dumps({'commit': head, 'subsystems': subs, 'functions': fs}, separators=(',', ':'))
    t = open(os.path.join(ROOT, 'tools', 'progress.html')).read()
    t = t.replace('/*ICON*/', logo_png()).replace('/*DATA*/', data.replace('</', '<\\/'))
    open(path, 'w').write(t)


def write_svg(path, done, total):
    """The README badge: the logo, WW.EXE, and a bar filled to the byte-identical share."""
    pct = 100.0 * done / total
    w = 290 * pct / 100
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        'width="320" height="240" viewBox="0 0 320 240">\n'
        '<rect width="320" height="240" rx="8" fill="#00004a"/>\n'
        '<image x="15" y="8" width="290" height="145" style="image-rendering:pixelated" '
        'xlink:href="data:image/png;base64,%s"/>\n'
        '<text x="160" y="180" text-anchor="middle" font-family="monospace" font-weight="bold" font-size="22" '
        'fill="#f8c840">WW.EXE</text>\n'
        '<clipPath id="fill"><rect x="15" y="196" width="%.2f" height="24"/></clipPath>\n'
        '<rect x="15" y="196" width="290" height="24" fill="#000" stroke="#eef0ff" stroke-width="1.5"/>\n'
        '<text x="160" y="213" text-anchor="middle" font-family="monospace" font-weight="bold" font-size="14" '
        'fill="#eef0ff">%.1f%%</text>\n'
        '<g clip-path="url(#fill)"><rect x="15" y="196" width="290" height="24" fill="#f8c840"/>\n'
        '<text x="160" y="213" text-anchor="middle" font-family="monospace" font-weight="bold" font-size="14" '
        'fill="#00004a">%.1f%%</text></g>\n'
        '</svg>\n') % (logo_png(), w, pct, pct)
    open(path, 'w').write(svg)


def main():
    out = os.path.join(ROOT, 'PROGRESS.md')
    if '--out' in sys.argv:
        out = sys.argv[sys.argv.index('--out') + 1]

    chk = run(['src/check.sh'])
    funcs = {}                                   # game address -> (name, size, matched)
    for name, size, kind, addr in re.findall(
            r'^(\S+)\s+(\d+) bytes  (MATCH at|differs from) (0x[0-9a-f]+)', chk, re.M):
        funcs[int(addr, 16)] = (name.rstrip('_'), int(size), kind == 'MATCH at')
    if not funcs:
        sys.exit('src/check.sh printed no comparisons:\n' + chk[-2000:])

    # the game's C functions src/ww.c does not have: funcs.py's 4-aligned starts in the C range
    # that fall inside none of ours
    named = names_file()
    found = [int(line.split('\t')[0], 16) for line in open(os.path.join(ROOT, '.work', 'le', 'ww_funcs.tsv'))
             if line.startswith('0x')]
    spans = sorted((a, a + s) for a, (_, s, _) in funcs.items())
    missing = {}
    for a in found:
        if C_START <= a < ASM_START and a % 4 == 0 and not any(s <= a < e for s, e in spans):
            missing[a] = named.get(a, 'f%x' % a)
    asm_found = sum(ASM_START <= a < LIB_START for a in found)

    al = align_all({a: n for a, (n, _, ok) in funcs.items() if not ok})
    rows = {a: v['d'] for a, v in al.items()}

    lib = run([PY, 'tools/wc/libmatch.py', 'ww', CLIB], timeout=1200)
    m = re.search(r'found\s+(\d+) \((\d+) bytes\)', lib)
    clib_mods, clib_bytes = (int(m.group(1)), int(m.group(2))) if m else (None, None)

    order, sub_of = subsystems()
    allf = [(a, n, 'match' if ok else 'near') for a, (n, _, ok) in funcs.items()]
    allf += [(a, n, 'none') for a, n in missing.items()]
    allf.sort()
    total = len(allf)
    done = sum(1 for _, _, s in allf if s == 'match')
    near = sum(1 for _, _, s in allf if s == 'near')
    head = run(['git', 'rev-parse', '--short', 'HEAD']).strip()

    def pct(a, b):
        return '%.1f%%' % (100.0 * a / b) if b else '-'

    L = []
    w = L.append
    summary = []
    w('# Progress')
    w('')
    w('How much of `WW.EXE` is rebuilt from C. Generated by `tools/progress.py` (do not edit by hand),')
    w('measured at commit `%s`.' % head)
    w('')
    w('**%d of the game\'s %d C functions compile to the original\'s exact bytes (%s).**' % (done, total, pct(done, total)))
    w('')
    w('@@SUMMARY@@')
    w('')
    w('A function counts only when our build of `src/ww.c` (Watcom C 9.5b, `-s -3r -oneatx`) gives')
    w('the same bytes as the game at the same address, with every address it refers to checked as')
    w('well; a call or jump between two of our own functions counts when it lands on the same function')
    w('and offset as the game\'s (only its distance depends on where our build puts the target).')
    w('A near miss behaves the same as far as we know, but its code still differs: the number')
    w('is how many instructions differ (`tools/wc/fndiff.py`).')
    w('')
    w('To search and filter the functions and see each near miss\'s diff, open')
    w('[`docs/WWPROGRESS.HTML`](docs/WWPROGRESS.HTML) in a browser.')
    w('')
    w('## WW.EXE part by part')
    w('')
    w('| Part | Code | Status |')
    w('|---|---|---|')
    def num(x):
        return '{:,}'.format(x)

    w('| The game\'s C, `src/ww.c` | 0x%05X-0x%05X, %s bytes | %d of %d functions byte-identical (%s), %d near misses, %d not written yet |'
      % (C_START, ASM_START - 1, num(ASM_START - C_START), done, total, pct(done, total), near, total - done - near))
    asmok = run([PY, 'tools/asm/asmcheck.py']).startswith('OK')
    if asmok:
        src = open(os.path.join(ROOT, 'src', 'wwasm.asm')).read()
        ents = set(re.findall(r'^\s+global (\w+)', src, re.M))
        named = len(re.findall(r'^; ---- 0x', src, re.M))
        w('| The game\'s assembler (VGA blitters, interrupt handlers), `src/wwasm.asm` | 0x%05X-0x%05X, %s bytes | '
          'reassembled byte for byte (nasm; `tools/asm/asmcheck.py`) and linked in place of the game\'s bytes; '
          '%d routines read and named (%d used by the C) |'
          % (ASM_START, LIB_START - 1, num(LIB_START - ASM_START), named, len(ents)))
    else:
        w('| The game\'s assembler (VGA blitters, interrupt handlers) | 0x%05X-0x%05X, %s bytes | not rebuilt: %d routines found |'
          % (ASM_START, LIB_START - 1, num(LIB_START - ASM_START), asm_found))
    if clib_mods is not None:
        w('| Watcom C library (`CLIB3R.LIB` 9.5b) | inside 0x%05X-0x%05X | %d modules, %s bytes, identified byte for byte; linked from the library, nothing to rewrite |'
          % (LIB_START, CODE_END - 1, clib_mods, num(clib_bytes)))
        mth = run([PY, 'tools/wc/libmatch.py', 'ww', MATHLIB], timeout=1200)
        m = re.search(r'found\s+(\d+) \((\d+) bytes\)', mth)
        math_mods, math_bytes = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
        w('| Watcom math library (`MATH387R.LIB` 9.5b) | inside 0x%05X-0x%05X | %d modules, %s bytes, identified byte for byte |'
          % (LIB_START, CODE_END - 1, math_mods, num(math_bytes)))
        nf = open(os.path.join(ROOT, 'analysis', 'ww_names.txt')).read()
        # the sound library: functions byte-identical to a released LIB (tools/wc/assmatch.py, the GF1 SDK
        # among them) and functions our src/ass port builds byte-identical (tools/ass/asscheck.py), by address
        snd = {int(a, 16): int(n) for a, n in re.findall(
            r'^0x([0-9a-f]+) +\S+ +# CONFIRMED: byte-identical \(fixups masked\) to ASS \S+ \S+, (\d+) bytes', nf, re.M)}
        lib_only = len(snd)
        ac = run([PY, 'tools/ass/asscheck.py', '-v'], timeout=3600)
        port = {int(a, 16): int(n) for n, a in re.findall(r'^\s+\S+\s+(\d+) bytes\s+MATCH at 0x([0-9a-f]+)', ac, re.M)}
        snd.update(port)
        m = re.search(r'^ASS: (\d+) of (\d+) named functions byte-identical; (\d+) modules identical as a whole', ac, re.M)
        port_ok, port_named, port_whole = (int(x) for x in m.groups()) if m else (0, 0, 0)
        port_near = sum(int(x) for x in re.findall(r'MATCH,\s+(\d+) near miss', ac))
        w('| The Apogee Sound System at Wacky Wheels\' revision, `src/ass` (with the game\'s own KLM music player) '
          '| inside 0x%05X-0x%05X | rebuilt from source: %d of %d functions byte-identical, %d modules identical '
          'as a whole, %d near misses (`tools/ass/asscheck.py`) |'
          % (LIB_START, CODE_END - 1, port_ok, port_named, port_whole, port_near))
        w('| Sound library, all identified code | inside 0x%05X-0x%05X | %d functions, %s bytes: the port\'s matches '
          'plus %d more byte-identical to a released ASS library or the Gravis GF1 SDK (`tools/wc/assmatch.py`), '
          'linked from the library |'
          % (LIB_START, CODE_END - 1, len(snd), num(sum(snd.values())), len(set(snd) - set(port))))
        m = re.search(r'emu387 +# CONFIRMED \(iter 92\): Watcom 9.5b \S+ module emu387, (\d+) bytes', nf)
        emu = int(m.group(1)) if m else 0
        if emu:
            w('| Watcom 387 emulator (`EMU387.LIB` 9.5b, module emu387) | 0x57E2C | %s bytes, the code byte for byte '
              '(a 0x77-byte table at its start is filled in the game, zero in the LIB) |' % num(emu))
        rest = CODE_END - LIB_START - clib_bytes - math_bytes - sum(snd.values()) - emu
        w('| Not byte-identical yet (the sound library\'s near misses, padding between modules, tables) '
          '| inside 0x%05X-0x%05X | %s bytes |' % (LIB_START, CODE_END - 1, num(rest)))
        c_bytes = sum(sz for _, (_, sz, ok) in funcs.items() if ok)
        asm_bytes = (LIB_START - ASM_START) if asmok else 0
        whole = CODE_END - C_START
        overall = c_bytes + asm_bytes + (whole - (LIB_START - C_START) - rest)
        summary.append('Of WW.EXE\'s whole code object (%s bytes), %s bytes (%s) are rebuilt byte for byte or '
                       'identified as byte-identical library code: the game\'s C functions that match, the '
                       'reassembled assembler, the Watcom libraries and the sound library.'
                       % (num(whole), num(overall), pct(overall, whole)))
    w('')
    w('## src/ww.c by subsystem')
    w('')
    w('The game\'s C was one source file (findings/executables.md), so the subsystems are groups of')
    w('functions, not files. The grouping is ours: `analysis/ww_subsystems.txt`.')
    w('')
    w('| Subsystem | Functions | Byte-identical | Near misses | Not written | Done |')
    w('|---|---:|---:|---:|---:|---:|')
    groups = order + (['unsorted'] if any(n not in sub_of for _, n, _ in allf) else [])
    for g in groups:
        fs = [s for _, n, s in allf if sub_of.get(n, 'unsorted') == g]
        if not fs:
            continue
        m_, n_, x_ = fs.count('match'), fs.count('near'), fs.count('none')
        w('| %s | %d | %d | %d | %d | %s |' % (g, len(fs), m_, n_, x_, pct(m_, len(fs))))
    w('| **All** | **%d** | **%d** | **%d** | **%d** | **%s** |' % (total, done, near, total - done - near, pct(done, total)))
    w('')
    w('## Every function')
    w('')
    w('In address order. Names without a meaning yet keep their address (`fn_1c0f4`); the evidence for')
    w('each name is in `analysis/ww_names.txt`.')
    w('')
    w('| Address | Function | Subsystem | Status |')
    w('|---|---|---|---|')
    for a, n, s in allf:
        st = {'match': 'byte-identical', 'none': 'not in src/ww.c yet'}.get(s)
        if s == 'near':
            st = 'near miss, %s instructions differ' % rows[a] if rows.get(a) is not None else 'near miss'
        w('| 0x%05X | `%s` | %s | %s |' % (a, n, sub_of.get(n, 'unsorted'), st))
    w('')
    text = '\n'.join(L).replace('@@SUMMARY@@\n\n', ''.join(x + '\n\n' for x in summary))
    open(out, 'w').write(text)
    write_html(os.path.join(ROOT, 'docs', 'WWPROGRESS.HTML'), allf, sub_of, order, al, head)
    write_svg(os.path.join(ROOT, 'docs', 'WWPROGRESS.SVG'), done, total)
    print('%s: %d of %d byte-identical (%s), %d near misses, %d not written'
          % (os.path.relpath(out, ROOT), done, total, pct(done, total), near, total - done - near))


if __name__ == '__main__':
    main()

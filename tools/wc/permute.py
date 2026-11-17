#!/usr/bin/env python3
"""permute.py: random-mode source permuter for one near miss (after decomp-permuter's random mode).

usage: permute.py NAME 0xADDR 0xEND [ROUNDS [COPIES [SEED]]]

Each round writes src/ww.c plus COPIES mutated copies of NAME (renamed pmNNN) into .work/perm/PERM.C,
compiles it once with the game's options and scores every copy against the game's bytes in
[ADDR, END) the way variants.py does (instruction rows, stack displacements included). The best copy
becomes the parent of the next round (ties are kept with probability 1/2, so the search can drift).
Every new best is written to .work/perm/best_<score>.c with the list of mutations that produced it.

Mutations marked '!' can change what the code does (statement swaps, int/short flips): a winner that
uses one must be checked by hand before it goes into src/ww.c. Rank here, decide in src/ww.c
(GOTCHAS "Rank in a probe, decide in src/ww.c").
"""
import os, random, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
import json, difflib
import capstone
import libmatch as L
import objmatch as M

name, addr, endg = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
rounds = int(sys.argv[4]) if len(sys.argv) > 4 else 10
copies = int(sys.argv[5]) if len(sys.argv) > 5 else 30
rng = random.Random(int(sys.argv[6]) if len(sys.argv) > 6 else 1)
work = os.path.join(ROOT, '.work', 'perm', name[:8].upper())
os.makedirs(work, exist_ok=True)

src = open(os.path.join(ROOT, 'src', 'ww.c')).read()
m = re.search(r'^[A-Za-z_][\w \*]*\b%s\s*\([^;{]*\)\s*\n\{' % re.escape(name), src, re.M)
if not m:
    sys.exit('definition of %s not found' % name)
fstart = m.start()
fend = src.index('\n}\n', m.end()) + 3
func = src[fstart:fend]

meta = json.load(open(os.path.join(ROOT, '.work', 'le', 'ww.json')))
code = open(os.path.join(ROOT, '.work', 'le', 'ww.obj1.bin'), 'rb').read()
cbase = meta['objects'][0]['base']
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def norm(s):
    return re.sub(r'0x[0-9a-f]{4,}', '@', s)


def dis(buf, vma, upto):
    return [(i.address, i.size, norm(i.mnemonic + ' ' + i.op_str))
            for i in md.disasm(buf, vma) if i.address < upto]


game = [t[2] for t in dis(code[addr - cbase:endg - cbase], addr, endg)]


def score_obj(path, names):
    (_, recs), = list(L.modules(open(path, 'rb').read()))
    segs = L.code_of(recs)
    pubs = sorted(p for p in M.publics(recs) if p[0] in segs)
    out = {}
    for nm in names:
        hit = [p for p in pubs if M.plain(p[2]) == nm or p[2] == nm]
        if not hit:
            continue
        seg, off, _ = hit[0]
        end = next((q[1] for q in pubs if q[0] == seg and q[1] > off), len(segs[seg][0]))
        buf, mask = segs[seg]
        ours = bytes(b if not mask[off + k] else 0x90 for k, b in enumerate(buf[off:end]))
        A = [t[2] for t in dis(ours, addr, endg)]
        sm = difflib.SequenceMatcher(None, A, game, autojunk=False)
        out[nm] = sum(max(i2 - i1, j2 - j1) for tag, i1, i2, j1, j2 in sm.get_opcodes() if tag != 'equal')
    return out


# ---- mutations: each takes the function text, returns (new text, label) or None
SIMPLE = r'[A-Za-z_][\w]*(?:\[[^\[\]]{1,20}\])?(?:(?:->|\.)\w+)*'


def sites(pat, body):
    return list(re.finditer(pat, body))


def m_swap_cmp(t):
    s = sites(r'(?<![\w\]\)])(%s|\d+|0x[0-9a-fA-F]+) (==|!=) (%s|\d+|0x[0-9a-fA-F]+)(?![\w\[\(])' % (SIMPLE, SIMPLE), t)
    if not s:
        return None
    x = rng.choice(s)
    return t[:x.start()] + '%s %s %s' % (x.group(3), x.group(2), x.group(1)) + t[x.end():], 'swap ' + x.group(0)


def m_swap_comm(t):
    s = sites(r'(?<![\w\]\)\-])(%s|\d+) ([+*&|]) (%s|\d+)(?![\w\[\(])' % (SIMPLE, SIMPLE), t)
    if not s:
        return None
    x = rng.choice(s)
    return t[:x.start()] + '%s %s %s' % (x.group(3), x.group(2), x.group(1)) + t[x.end():], 'comm ' + x.group(0)


def m_not(t):
    forms = [(r'if \(!(%s)\)' % SIMPLE, r'if (\1 == 0)'), (r'if \((%s) == 0\)' % SIMPLE, r'if (!\1)'),
             (r'if \((%s)\)' % SIMPLE, r'if (\1 != 0)'), (r'if \((%s) != 0\)' % SIMPLE, r'if (\1)')]
    rng.shuffle(forms)
    for pat, rep in forms:
        s = sites(pat, t)
        if s:
            x = rng.choice(s)
            return t[:x.start()] + re.sub(pat, rep, x.group(0)) + t[x.end():], 'not ' + x.group(0)
    return None


def m_incr(t):
    s = sites(r'(?<=[\s;(])(\w+)(\+\+|--|\s\+= 1|\s-= 1)(?=[;)])', t)
    if not s:
        return None
    x = rng.choice(s)
    v, op = x.group(1), x.group(2).strip()
    plus = op in ('++', '+= 1')
    choices = ['%s%s' % ('++' if plus else '--', v), '%s %s 1' % (v, '+=' if plus else '-='),
               '%s = %s %s 1' % (v, v, '+' if plus else '-'), v + ('++' if plus else '--')]
    new = rng.choice([c for c in choices if c != x.group(0)])
    return t[:x.start()] + new + t[x.end():], 'incr %s -> %s' % (x.group(0), new)


def m_bound(t):
    s = sites(r'(<=|<|>=|>) (\d+)\)', t)
    if not s:
        return None
    x = rng.choice(s)
    op, n = x.group(1), int(x.group(2))
    new = {'<=': '< %d' % (n + 1), '<': '<= %d' % (n - 1), '>=': '> %d' % (n - 1), '>': '>= %d' % (n + 1)}[op]
    return t[:x.start()] + new + ')' + t[x.end():], 'bound %s -> %s' % (x.group(0), new)


def m_decl(t):
    head, brace, body = t.partition('{')
    lines = body.split('\n')
    decl = [i for i, l in enumerate(lines[:40]) if re.match(r'\s+(?:unsigned |signed )?(?:int|short|char|long)\b[^(=]*;', l)
            and '[' not in l]
    if not decl:
        return None
    i = rng.choice(decl)
    mm = re.match(r'(\s+(?:unsigned |signed )?(?:int|short|char|long)\s+)([^;]*);(.*)', lines[i])
    names = [n.strip() for n in mm.group(2).split(',')]
    if len(names) > 1 and rng.random() < 0.6:
        rng.shuffle(names)
        lines[i] = mm.group(1) + ', '.join(names) + ';' + mm.group(3)
        lab = 'declorder ' + ', '.join(names)
    elif len(decl) > 1:
        j = rng.choice([d for d in decl if d != i])
        lines[i], lines[j] = lines[j], lines[i]
        lab = 'declswap %d %d' % (i, j)
    else:
        return None
    return head + brace + '\n'.join(lines), lab


def m_type(t):
    s = sites(r'\n(\s+)(int|short) (\w+);', t)
    if not s:
        return None
    x = rng.choice(s)
    nt = 'short' if x.group(2) == 'int' else 'int'
    return t[:x.start()] + '\n%s%s %s;' % (x.group(1), nt, x.group(3)) + t[x.end():], '!type %s %s' % (nt, x.group(3))


STMT = re.compile(r'^\s+[^{}\n;]*;\s*(/\*.*\*/)?\s*$')


def m_swapstmt(t):
    lines = t.split('\n')
    c = [i for i in range(len(lines) - 1)
         if STMT.match(lines[i]) and STMT.match(lines[i + 1])
         and not re.search(r'\b(return|break|continue|goto|int|short|char|long)\b', lines[i] + lines[i + 1])
         and len(lines[i]) - len(lines[i].lstrip()) == len(lines[i + 1]) - len(lines[i + 1].lstrip())]
    if not c:
        return None
    i = rng.choice(c)
    lines[i], lines[i + 1] = lines[i + 1], lines[i]
    return '\n'.join(lines), '!stmtswap ' + lines[i + 1].strip()[:40]


MUTS = [m_swap_cmp, m_swap_comm, m_not, m_incr, m_bound, m_decl, m_type, m_swapstmt]


def mutate(t):
    labs = []
    for _ in range(rng.choice([1, 1, 1, 2, 2, 3])):
        for _ in range(10):
            r = rng.choice(MUTS)(t)
            if r:
                t, lab = r
                labs.append(lab)
                break
    return t, labs


def compile_round(cands):
    body = [re.sub(r'\b%s\s*\(' % re.escape(name), 'pm%03d(' % k, c, count=1) for k, (c, _) in enumerate(cands)]
    probe = src[:fend] + '\n' + '\n'.join(body) + src[fend:]
    open(os.path.join(work, 'PERM.C'), 'w').write(probe)
    obj = os.path.join(work, 'PERM.OBJ')
    if os.path.exists(obj):
        os.remove(obj)
    subprocess.run([os.path.join(ROOT, 'tools', 'wc', 'wcc.sh'), os.path.join(ROOT, '.work', 'toolchains', 'inst', 'WC95B'),
                    work, 'PERM.C', '-s', '-3r', '-oneatx'], capture_output=True, text=True)
    if not os.path.exists(obj):
        return None
    sc = score_obj(obj, ['pm%03d' % k for k in range(len(cands))] + [name])
    return sc


parent, plabs = func, []
sc = compile_round([(func, [])])
if not sc:
    sys.exit('control compile failed (see .work/perm/WCC.OUT)')
best = sc['pm000']
print('control %s: %d rows (original in the same object: %s)' % (name, best, sc.get(name)), flush=True)
for r in range(rounds):
    cands = [(parent, plabs)]
    seen = {parent}
    while len(cands) < copies:
        t, labs = mutate(parent)
        if t not in seen:
            seen.add(t)
            cands.append((t, plabs + labs))
    sc = compile_round(cands)
    if not sc:
        # a mutant that does not compile kills the round: halve the batch to find it next time
        print('round %d: compile failed' % r, flush=True)
        continue
    ranked = sorted((sc.get('pm%03d' % k, 9999), k) for k in range(len(cands)))
    s, k = ranked[0]
    print('round %d: best %d (parent %d) %s' % (r, s, sc.get('pm000'), cands[k][1][-3:]), flush=True)
    if s < best or (s == best and k and rng.random() < 0.5):
        if s < best:
            open(os.path.join(work, 'best_%s_%d.c' % (name, s)), 'w').write(
                '/* %s */\n' % '; '.join(cands[k][1]) + cands[k][0])
        best, parent, plabs = s, cands[k][0], cands[k][1]
    if best == 0:
        print('ZERO ROWS - verify with src/check.sh', flush=True)
        break
print('final best %d: %s' % (best, '; '.join(plabs)))

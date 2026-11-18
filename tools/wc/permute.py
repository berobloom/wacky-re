#!/usr/bin/env python3
"""permute.py: random-mode source permuter for one near miss (after decomp-permuter's random mode).

usage: permute.py [--weighted] [--slack N] [--hill] NAME 0xADDR 0xEND [ROUNDS [COPIES [SEED]]]

Each round writes src/ww.c plus COPIES mutated copies of NAME (renamed pmNNN) into
.work/perm/NAME/PERM.C, compiles it once with the game's options and scores every copy against the
game's bytes in [ADDR, END) the way variants.py does (instruction rows, stack displacements included).

Search (iter 56, after decomp-permuter's random walk): the walk's current position and the best are
kept apart. A sixth of each round's copies restart from the best, the rest mutate the current
position; the round's winner becomes the current position when it is no worse, or with
probability 0.3 when it is at most SLACK worse (default 6 rows, or 6 * 5 points with --weighted).
--hill restores the old rule (climb from the best only, ties kept with probability 1/2).
A source text already scored is not compiled again (cache by text).
--weighted ranks by decomp-permuter's scorer weights instead of rows: 5 per differing pair with the
same mnemonic (a register or slot), 60 per row that only moved, 100 per inserted or missing row.
Rows are still what is printed and what names best_<NAME>_<rows>.c.
Every new best (in rows) is written to .work/perm/NAME/best_<NAME>_<rows>.c with its mutations.

Mutations marked '!' can change what the code does (statement swaps, int/short flips): a winner that
uses one must be checked by hand before it goes into src/ww.c. Rank here, decide in src/ww.c
(GOTCHAS "Rank in a probe, decide in src/ww.c").
"""
import functools, os, random, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
import json, difflib
import capstone
import libmatch as L
import objmatch as M

args = sys.argv[1:]
weighted = '--weighted' in args
hill = '--hill' in args
slack = None
if '--slack' in args:
    slack = int(args[args.index('--slack') + 1])
    del args[args.index('--slack'):args.index('--slack') + 2]
args = [a for a in args if a not in ('--weighted', '--hill')]
name, addr, endg = args[0], int(args[1], 16), int(args[2], 16)
rounds = int(args[3]) if len(args) > 3 else 10
copies = int(args[4]) if len(args) > 4 else 30
rng = random.Random(int(args[5]) if len(args) > 5 else 1)
if slack is None:
    slack = 30 if weighted else 6
work = os.path.join(ROOT, '.work', 'perm', name.upper())    # full name: name[:8] collided (netkeys_in/_out/_pack, iter 56)
os.makedirs(work, exist_ok=True)

src = open(os.path.join(ROOT, 'src', 'ww.c')).read()
cname = name                    # the C name of the definition
al = re.search(r'#pragma aux (\w+) "%s_"' % re.escape(name), src)
if al:
    cname = al.group(1)         # defined under an alias (showlines is showlines_def, iter 56)
m = re.search(r'^[A-Za-z_][\w \*]*\b%s\s*\([^;{]*\)[ \t]*(?:/\*.*?\*/)?\s*\n\{' % re.escape(cname), src, re.M)
if not m:
    sys.exit('definition of %s not found' % cname)
fstart = m.start()
fend = src.index('\n}\n', m.end()) + 3
func = src[fstart:fend]

meta = json.load(open(os.path.join(ROOT, '.work', 'le', 'ww.json')))
code = open(os.path.join(ROOT, '.work', 'le', 'ww.obj1.bin'), 'rb').read()
cbase = meta['objects'][0]['base']
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def norm(s):
    return re.sub(r'0x[0-9a-f]{4,}', '@', s).replace('- @', '+ @')  # masked fixup, see fndiff.norm


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
        out[nm] = rows_and_weight(A)
    return out


def rows_and_weight(A):
    # rows: what fndiff/variants count. weight: decomp-permuter's scorer.py constants -
    # PENALTY_REGALLOC 5 (a replaced pair with the same mnemonic), PENALTY_REORDERING 60 (a row
    # inserted on one side and deleted on the other), PENALTY_INSERTION/DELETION 100.
    sm = difflib.SequenceMatcher(None, A, game, autojunk=False)
    rows, ins, dels, w = 0, [], [], 0
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        rows += max(i2 - i1, j2 - j1)
        a, g = A[i1:i2], game[j1:j2]
        n = min(len(a), len(g)) if tag == 'replace' else 0
        for x, y in zip(a[:n], g[:n]):
            if x.split()[0] == y.split()[0]:
                w += 5
            else:
                ins.append(x); dels.append(y)
        ins += a[n:]; dels += g[n:]
    moved = 0
    pool = {}
    for x in ins:
        pool[x] = pool.get(x, 0) + 1
    for y in dels:
        if pool.get(y):
            pool[y] -= 1; moved += 1
    w += 60 * moved + 100 * (len(ins) - moved) + 100 * (len(dels) - moved)
    return rows, w


# ---- mutations: each takes the function text, returns (new text, label) or None
SIMPLE = r'[A-Za-z_][\w]*(?:\[[^\[\]]{1,20}\])?(?:(?:->|\.)\w+)*'


def sites(pat, body):
    return list(re.finditer(pat, body))


def m_swap_cmp(t):
    s = sites(r'(?<![\w\]\)\-\+\.>])(%s|\d+|0x[0-9a-fA-F]+) (==|!=) (%s|\d+|0x[0-9a-fA-F]+)(?![\w\[\(])' % (SIMPLE, SIMPLE), t)
    if not s:
        return None
    x = rng.choice(s)
    return t[:x.start()] + '%s %s %s' % (x.group(3), x.group(2), x.group(1)) + t[x.end():], 'swap ' + x.group(0)


def m_swap_comm(t):
    # Only a whole operand pair: the previous non-blank is ( [ , or a plain `=`, the next one ) ] ; ,
    # (iter 56: `- 1 + X` became `- X + 1`, and `a * b + c` could become `a * c + b`).
    s = [x for x in sites(r'(?<![\w\]\)\-])(%s|\d+) ([+*&|]) (%s|\d+)(?![\w\[\(])' % (SIMPLE, SIMPLE), t)
         if re.search(r'(?:[(\[,]|(?<![=!<>+\-*/&|^%])=)\s*$', t[:x.start()])
         and re.match(r'\s*[)\];,]', t[x.end():])]
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
    s = sites(r'(?<= )(<=|<|>=|>) (\d+)\)', t)       # not the `> 1)` of `>> 1)` (iter 56)
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


def independent(a, b):
    # iter 56: only swap two statements that share no identifier and call nothing - the unchecked
    # swap produced line_points (broke a dx/dy swap), netkeys_in (read c before its store), pic_at.
    code = lambda x: re.sub(r'/\*.*?\*/|"[^"]*"', '', x)
    a, b = code(a), code(b)
    if re.search(r'\w\s*\(', a + ' ' + b):
        return False
    ids = lambda x: set(re.findall(r'[A-Za-z_]\w*', x))
    return not (ids(a) & ids(b))


def m_swapstmt(t):
    lines = t.split('\n')
    c = [i for i in range(len(lines) - 1)
         if STMT.match(lines[i]) and STMT.match(lines[i + 1])
         and not re.search(r'\b(return|break|continue|goto|int|short|char|long)\b', lines[i] + lines[i + 1])
         and len(lines[i]) - len(lines[i].lstrip()) == len(lines[i + 1]) - len(lines[i + 1].lstrip())
         and independent(lines[i], lines[i + 1])]
    if not c:
        return None
    i = rng.choice(c)
    lines[i], lines[i + 1] = lines[i + 1], lines[i]
    return '\n'.join(lines), '!stmtswap ' + lines[i + 1].strip()[:40]


# iter 56 additions, after decomp-permuter's passes of the same names. Operands are SIMPLE lvalues
# whose index holds no call, ++ or -- (SAFE), so evaluating one once or twice is the same.
SAFE = r'[A-Za-z_]\w*(?:\[[^\[\]()+\-;]{1,20}\])?(?:(?:->|\.)\w+)*'
ARITH = r'\+|-|\*|&|\||\^|<<'


def m_compound(t):
    # perm_compound_assignment: x = x op y; <-> x op= y;  y one term (a SAFE lvalue or a number), so
    # precedence cannot differ (x = x * a + b is not x *= a + b)
    TERM = r'%s|\d+|0x[0-9a-fA-F]+' % SAFE
    OPS = ARITH + r'|>>|/|%'
    s = sites(r'\n(\s+)(%s) = \2 (%s) (%s);' % (SAFE, OPS, TERM), t) + \
        sites(r'\n(\s+)(%s) (%s)= (%s);' % (SAFE, OPS, TERM), t)
    if not s:
        return None
    x = rng.choice(s)
    ind, v, op, y = x.groups()
    if ' = ' in x.group(0):
        new = '\n%s%s %s= %s;' % (ind, v, op, y)
    else:
        new = '\n%s%s = %s %s %s;' % (ind, v, v, op, y)
    return t[:x.start()] + new + t[x.end():], 'compound %s -> %s' % (x.group(0).strip(), new.strip())


@functools.lru_cache(maxsize=None)
def pointerish(n):
    if re.fullmatch(r'\d+|0x[0-9a-fA-F]+', n):
        return False
    return bool(re.search(r'\*\s*%s\b|\b%s\s*\[|\b%s\s*\(' % (n, n, n), src))


def m_split(t):
    # perm_split_assignment: x = a op b; -> x = a; x op= b; with a and b single terms that do not
    # mention x, and + - * & | ^ << only (they agree with a narrow x's truncation in either order)
    TERM = r'%s|\d+|0x[0-9a-fA-F]+' % SAFE
    # x and a plain names that src/ww.c never declares as a pointer or array: pointer arithmetic
    # scales by the pointee, so p = arr + 0x28 is not p = arr; p += 0x28 when the two differ
    s = sites(r'\n(\s+)([A-Za-z_]\w*) = ([A-Za-z_]\w*|\d+|0x[0-9a-fA-F]+) (%s) (%s);' % (ARITH, TERM), t)
    s = [x for x in s if not (set(re.findall(r'\w+', x.group(2))) & set(re.findall(r'\w+', x.group(3) + ' ' + x.group(5))))
         and not any(pointerish(n) for n in (x.group(2), x.group(3)))]
    if not s:
        return None
    x = rng.choice(s)
    ind, v = x.group(1), x.group(2)
    new = '\n%s%s = %s;\n%s%s %s= %s;' % (ind, v, x.group(3), ind, v, x.group(4), x.group(5))
    return t[:x.start()] + new + t[x.end():], 'split %s' % x.group(0).strip()


def m_chain(t):
    # perm_chain_assignment: a = E; b = a; <-> b = a = E;
    s = [x for x in sites(r'\n(\s+)(%s) = ([^;\n=]+);\n\1(%s) = \2;' % (SAFE, SAFE), t) if x.group(2) != x.group(4)]
    r = sites(r'\n(\s+)(%s) = (%s) = ([^;\n=]+);' % (SAFE, SAFE), t)
    if not s and not r:
        return None
    if s and (not r or rng.random() < 0.5):
        x = rng.choice(s)
        new = '\n%s%s = %s = %s;' % (x.group(1), x.group(4), x.group(2), x.group(3))
    else:
        x = rng.choice(r)
        new = '\n%s%s = %s;\n%s%s = %s;' % (x.group(1), x.group(3), x.group(4), x.group(1), x.group(2), x.group(3))
    return t[:x.start()] + new + t[x.end():], 'chain %s' % x.group(0).strip()[:50]


def m_whilecond(t):
    # perm_condition for loops: while (x) <-> while (x != 0)
    forms = [(r'while \((%s)\)' % SIMPLE, r'while (\1 != 0)'), (r'while \((%s) != 0\)' % SIMPLE, r'while (\1)'),
             (r'while \(!(%s)\)' % SIMPLE, r'while (\1 == 0)'), (r'while \((%s) == 0\)' % SIMPLE, r'while (!\1)')]
    rng.shuffle(forms)
    for pat, rp in forms:
        s = sites(pat, t)
        if s:
            x = rng.choice(s)
            return t[:x.start()] + re.sub(pat, rp, x.group(0)) + t[x.end():], 'not ' + x.group(0)
    return None


def m_empty(t):
    # perm_empty_stmt: a no-op statement after a plain statement
    lines = t.split('\n')
    c = [i for i in range(len(lines)) if STMT.match(lines[i]) and not re.search(r'\b(int|short|char|long)\b', lines[i])]
    if not c:
        return None
    i = rng.choice(c)
    ind = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
    st = rng.choice([';', 'if (1) {}', 'if (0) {}', 'do {} while (0);'])
    lines.insert(i + 1, ind + st)
    return '\n'.join(lines), 'empty %s after %s' % (st, lines[i].strip()[:30])


def m_pad(t):
    # perm_pad_var_decl: an unused local declared first or last among the scalars
    head, brace, body = t.partition('{')
    lines = body.split('\n')
    decl = [i for i, l in enumerate(lines[:40]) if re.match(r'\s+(?:unsigned |signed )?(?:int|short|char|long)\b[^(=]*;', l)]
    if not decl or 'pmpad' in t:
        return None
    i = rng.choice([decl[0], decl[-1] + 1])
    lines.insert(i, '    %s pmpad;' % rng.choice(['int', 'short', 'char']))
    return head + brace + '\n'.join(lines), 'pad ' + lines[i].strip()


def m_cast(t):
    # perm_cast_simple: (int) on one operand of a comparison - '!' because an (int) on an unsigned
    # value changes how it compares
    s = sites(r'(?<=[(\s])(%s) (==|!=|<=|>=|<|>) ' % SAFE, t)
    s = [x for x in s if not re.match(r'\d', x.group(1))]
    if not s:
        return None
    x = rng.choice(s)
    return t[:x.start()] + '(int)%s %s ' % (x.group(1), x.group(2)) + t[x.end():], '!cast (int)' + x.group(1)


MUTS = [m_swap_cmp, m_swap_comm, m_not, m_incr, m_bound, m_decl, m_type, m_swapstmt,
        m_compound, m_split, m_chain, m_whilecond, m_empty, m_pad, m_cast]


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
    # Copies that do not compile are dropped by the error lines in WCC.OUT and the rest recompiled
    # (iter 56: one bad mutant used to kill the round - kart_bump lost 23 of 30 rounds that way).
    alive = list(range(len(cands)))
    for _ in range(6):
        body, spans, line = [], [], src[:fend].count('\n') + 2
        for k in alive:
            c = re.sub(r'\b%s\s*\(' % re.escape(cname), 'pm%03d(' % k, cands[k][0], count=1)
            spans.append((line, line + c.count('\n'), k))
            line += c.count('\n') + 1
            body.append(c)
        probe = src[:fend] + '\n' + '\n'.join(body) + src[fend:]
        open(os.path.join(work, 'PERM.C'), 'w').write(probe)
        obj = os.path.join(work, 'PERM.OBJ')
        if os.path.exists(obj):
            os.remove(obj)
        subprocess.run([os.path.join(ROOT, 'tools', 'wc', 'wcc.sh'), os.path.join(ROOT, '.work', 'toolchains', 'inst', 'WC95B'),
                        work, 'PERM.C', '-s', '-3r', '-oneatx'], capture_output=True, text=True)
        if os.path.exists(obj):
            return score_obj(obj, ['pm%03d' % k for k in alive] + [name])
        try:
            out = open(os.path.join(work, 'WCC.OUT'), errors='replace').read()
        except OSError:
            return None
        bad = {k for n in map(int, re.findall(r'PERM\.C\((\d+)\): Error', out))
               for a, b, k in spans if a <= n <= b + 1}
        if not bad or 0 in bad:
            return None
        alive = [k for k in alive if k not in bad]
    return None

def key(v):
    return v[1] if weighted else v[0]


sc = compile_round([(func, [])])
if not sc:
    sys.exit('control compile failed (see %s/WCC.OUT)' % work)
rows0 = sc['pm000'][0]
print('control %s: %d rows, weight %d (original in the same object: %s)'
      % (name, rows0, sc['pm000'][1], sc.get(name, (None,))[0]), flush=True)
cache = {func: sc['pm000']}
best_rows = rows0                               # what best_<NAME>_<rows>.c and the report use
best, best_t, best_l = sc['pm000'], func, []    # by key(): rows, or weight with --weighted
cur, cur_t, cur_l = best, func, []
for r in range(rounds):
    if hill:
        cur, cur_t, cur_l = best, best_t, best_l
    cands = [(cur_t, cur_l)]
    seen = {cur_t}
    tries = 0
    while len(cands) < copies and tries < 50 * copies:     # a small function may have < COPIES mutants
        tries += 1
        base_t, base_l = (best_t, best_l) if (not hill and rng.random() < 1 / 6) else (cur_t, cur_l)
        t, labs = mutate(base_t)
        if t in seen or t in cache:
            continue
        seen.add(t)
        cands.append((t, base_l + labs))
    sc = compile_round(cands)
    if not sc:
        print('round %d: compile failed' % r, flush=True)
        continue
    res = []
    for k, (t, labs) in enumerate(cands):
        v = sc.get('pm%03d' % k)
        if v is None:
            continue
        cache[t] = v
        res.append((key(v), v[0], k))
    if len(res) <= 1:
        print('round %d: no new mutants' % r, flush=True)
        continue
    res.sort()
    kv, rows, k = next(x for x in res if x[2] != 0)       # the round's best new candidate
    v = sc['pm%03d' % k]
    print('round %d: best %d rows / weight %d (current %d, best %d) %s'
          % (r, rows, v[1], cur[0], best_rows, cands[k][1][-3:]), flush=True)
    if rows < best_rows:
        best_rows = rows
        open(os.path.join(work, 'best_%s_%d.c' % (name, rows)), 'w').write(
            '/* %s */\n' % '; '.join(cands[k][1]) + cands[k][0])
    if kv < key(best) or (kv == key(best) and rows <= best[0]):
        best, best_t, best_l = v, cands[k][0], cands[k][1]
    if hill:
        if kv < key(cur) or (kv == key(cur) and rng.random() < 0.5):
            cur, cur_t, cur_l = v, cands[k][0], cands[k][1]
    elif kv <= key(cur) or (kv <= key(cur) + slack and rng.random() < 0.3):
        cur, cur_t, cur_l = v, cands[k][0], cands[k][1]
    if best_rows == 0:
        print('ZERO ROWS - verify with src/check.sh', flush=True)
        break
print('final best %d: %s' % (best_rows, '; '.join(best_l)))

"""Shared parts of the permuters (permute.py: regex mutations; astperm.py: syntax-tree mutations).

Target(name, addr, end) finds the definition in src/ww.c (through a `#pragma aux X "name_"` alias
if there is one), disassembles the game's bytes, and scores candidate texts of the function:
compile_round() appends them to a copy of src/ww.c as pm000.. after the original definition,
compiles once with WCC386 9.5b (-s -3r -oneatx), and scores each copy against the game in rows (what
fndiff/nearmiss count) and in decomp-permuter's scorer weight. walk() is the random walk both
permuters search with.
"""
import difflib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'le'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
import capstone
import libmatch as L
import objmatch as M

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def norm(s):
    return re.sub(r'0x[0-9a-f]{4,}', '@', s).replace('- @', '+ @')  # masked fixup, see fndiff.norm


def dis(buf, vma, upto):
    return [(i.address, i.size, norm(i.mnemonic + ' ' + i.op_str))
            for i in md.disasm(buf, vma) if i.address < upto]


def rows_and_weight(A, game):
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


class Target:
    def __init__(self, name, addr, end, workdir='perm'):
        self.name, self.addr, self.end = name, addr, end
        # full name: name[:8] collided (netkeys_in/_out/_pack, iter 56)
        self.work = os.path.join(ROOT, '.work', workdir, name.upper())
        os.makedirs(self.work, exist_ok=True)
        self.src = src = open(os.path.join(ROOT, 'src', 'ww.c')).read()
        # The C name of the definition: NAME, or an alias `#pragma aux X "NAME_"` that is defined
        # (showlines is showlines_def, iter 56); most aliases are call sites only (track_setup_x).
        m = None
        for cn in [a for a in re.findall(r'#pragma aux (\w+) "%s_"' % re.escape(name), src)] + [name]:
            m = re.search(r'^[A-Za-z_][\w \*]*\b%s\s*\([^;{]*\)[ \t]*(?:/\*.*?\*/)?\s*\n\{'
                          % re.escape(cn), src, re.M)
            if m:
                self.cname = cn
                break
        if not m:
            sys.exit('definition of %s not found' % name)
        self.fstart = m.start()
        self.fbody = m.end() - 1            # the body's opening brace
        self.fend = src.index('\n}\n', m.end()) + 3
        self.func = src[self.fstart:self.fend]
        self.head = src[self.fstart:self.fbody]   # the signature, kept as written
        meta = json.load(open(os.path.join(ROOT, '.work', 'le', 'ww.json')))
        code = open(os.path.join(ROOT, '.work', 'le', 'ww.obj1.bin'), 'rb').read()
        cbase = meta['objects'][0]['base']
        self.game = [t[2] for t in dis(code[addr - cbase:end - cbase], addr, end)]

    def score_obj(self, path, names):
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
            A = [t[2] for t in dis(ours, self.addr, self.end)]
            out[nm] = rows_and_weight(A, self.game)
        return out

    def compile_round(self, cands):
        # Copies that do not compile are dropped by the error lines in WCC.OUT and the rest recompiled
        # (iter 56: one bad mutant used to kill the round - kart_bump lost 23 of 30 rounds that way).
        src, fend, work = self.src, self.fend, self.work
        alive = list(range(len(cands)))
        for _ in range(6):
            body, spans, line = [], [], src[:fend].count('\n') + 2
            for k in alive:
                c = re.sub(r'\b%s\s*\(' % re.escape(self.cname), 'pm%03d(' % k, cands[k][0], count=1)
                spans.append((line, line + c.count('\n'), k))
                line += c.count('\n') + 1
                body.append(c)
            # A last, unscored copy: the code after a function's ret can belong to the function that
            # follows (screenshot's 836 bytes end with an epilogue the next function jumps into,
            # 0x11806), so only a copy followed by another copy is scored like the others (iter 107:
            # without it the last copy of every round scored 4 rows better on screenshot).
            body.append(re.sub(r'\b%s\s*\(' % re.escape(self.cname), 'pmend(', cands[alive[0]][0], count=1))
            probe = src[:fend] + '\n' + '\n'.join(body) + src[fend:]
            open(os.path.join(work, 'PERM.C'), 'w').write(probe)
            obj = os.path.join(work, 'PERM.OBJ')
            if os.path.exists(obj):
                os.remove(obj)
            subprocess.run([os.path.join(ROOT, 'tools', 'wc', 'wcc.sh'),
                            os.path.join(ROOT, '.work', 'toolchains', 'inst', 'WC95B'),
                            work, 'PERM.C', '-s', '-3r', '-oneatx'], capture_output=True, text=True)
            if os.path.exists(obj):
                return self.score_obj(obj, ['pm%03d' % k for k in alive] + [self.name])
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


def walk(T, start, text_of, mutate, rounds, copies, rng, weighted=False, slack=None, hill=False,
         control=None, best_prefix='best'):
    """decomp-permuter's random walk. States are opaque: text_of(state) is the candidate's source
    (the whole definition), mutate(state) -> (state, labels). A sixth of each round's copies restart
    from the best, the rest mutate the current position, which follows the round's winner when it is
    no worse, and with probability 0.3 when it is at most SLACK worse. hill: climb from the best only.
    Writes <work>/<best_prefix>_<name>_<rows>.c for every new best and returns (best_rows, labels)."""
    if slack is None:
        slack = 30 if weighted else 6
    key = (lambda v: v[1]) if weighted else (lambda v: v[0])
    func = text_of(start)
    if control is None:
        control = T.compile_round([(func, [])])
        if not control:
            sys.exit('control compile failed (see %s/WCC.OUT)' % T.work)
        control = control['pm000']
    cache = {func: control}
    best_rows = control[0]
    best, best_s, best_l = control, start, []
    cur, cur_s, cur_l = best, start, []
    for r in range(rounds):
        if hill:
            cur, cur_s, cur_l = best, best_s, best_l
        cands, states = [(text_of(cur_s), cur_l)], [cur_s]
        seen = {cands[0][0]}
        tries = 0
        while len(cands) < copies and tries < 50 * copies:     # a small function may have < COPIES mutants
            tries += 1
            base_s, base_l = (best_s, best_l) if (not hill and rng.random() < 1 / 6) else (cur_s, cur_l)
            s, labs = mutate(base_s)
            if s is None:
                continue
            t = text_of(s)
            if t in seen or t in cache:
                continue
            seen.add(t)
            cands.append((t, base_l + labs))
            states.append(s)
        sc = T.compile_round(cands)
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
        print('round %d: best %d rows / weight %d (current %d, best %d; %d/%d compiled) %s'
              % (r, rows, v[1], cur[0], best_rows, len(res), len(cands), cands[k][1][-3:]), flush=True)
        if rows < best_rows:
            best_rows = rows
            open(os.path.join(T.work, '%s_%s_%d.c' % (best_prefix, T.name, rows)), 'w').write(
                '/* %s */\n' % '; '.join(cands[k][1]) + cands[k][0])
        if kv < key(best) or (kv == key(best) and rows <= best[0]):
            best, best_s, best_l = v, states[k], cands[k][1]
        if hill:
            if kv < key(cur) or (kv == key(cur) and rng.random() < 0.5):
                cur, cur_s, cur_l = v, states[k], cands[k][1]
        elif kv <= key(cur) or (kv <= key(cur) + slack and rng.random() < 0.3):
            cur, cur_s, cur_l = v, states[k], cands[k][1]
        if best_rows == 0:
            print('ZERO ROWS - verify with src/check.sh', flush=True)
            break
    return best_rows, best_l

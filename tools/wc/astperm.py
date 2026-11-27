#!/usr/bin/env python3
"""astperm.py: syntax-tree permuter for one near miss (decomp-permuter's randomizer on src/ww.c).

usage: astperm.py [--all] [--only P1,P2,..] [--weighted] [--slack N] [--hill]
                  NAME 0xADDR 0xEND [ROUNDS [COPIES [SEED]]]
       astperm.py --check LIST      # round trip only: every function of LIST printed in place
       astperm.py --reduce FILE NAME 0xADDR 0xEND   # shrink a winner: undo each change hunk (against
                                    # the printed original) in place while the rows do not rise
                                    # (LIST lines "name ourbytes 0xADDR", as permute_all.sh)

permute.py mutates the function's text with regexes; this one parses src/ww.c into a C syntax tree
(cpp with the WC95B headers, then decomp-permuter's pycparser fork) and mutates NAME's tree with
decomp-permuter's own randomization passes - the statement-level ones permute.py has no regex
for: a temporary for a subexpression assigned somewhere before its use (perm_temp_for_expr, the
main pass), a variable replaced by its value, split and chained assignments, `if (v) {}` and
other no-op blocks around statements, struct reference forms, and so on. Each candidate's body is
printed from the tree and spliced under NAME's own signature (kept as written: __interrupt etc.),
then compiled and scored by tools/wc/permlib.py with the same random walk as permute.py.

The printed body is the preprocessed one: macros are expanded, comments gone, braces everywhere.
Before the walk, the printed but unmutated body is compiled next to the original: if the two do
not score the same, the round trip changed the code and the run says so (it still starts from the
printed body, whose score is then the control).

Passes (default weights from decomp-permuter's default_weights.toml [base]). Left out by default,
because they change what the code does or cannot work on a body alone: reorder_stmts,
remove_ast, remove_var, add_mask, cast_simple, randomize_internal_type, factor_mult,
duplicate_assignment, split_assignment (unsafe; split_assignment broke `x -= 2` in iter 107), and
temp_for_expr's reuse of an existing variable as its temporary (it reused a live one); randomize_external_type, randomize_function_type, inline (change
code outside the body); float_literal, sameline (nothing for them here). --all adds the unsafe
ones; --only runs just the named passes (without the perm_ prefix). Labels marked '!' are passes
that can change behaviour (temp_for_expr and expand_expr move or copy an evaluation and do not
check calls in between): read the winner's diff before it goes into src/ww.c (MATCHING.md 7).

Needs .work/decomp-permuter (MIT; clone https://github.com/simonlindholm/decomp-permuter, tested at
860b6d9). Every new best goes to .work/perm_ast/NAME/ast_<NAME>_<rows>.c.
"""
import copy
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
import permlib

DP = os.path.join(ROOT, '.work', 'decomp-permuter')
if not os.path.isdir(os.path.join(DP, 'src')):
    sys.exit('needs %s: git clone https://github.com/simonlindholm/decomp-permuter %s' % (DP, DP))
sys.path.insert(0, DP)
from src import ast_util
from src import randomizer as R
from perm_pycparser import c_ast as ca

UNSAFE = {'reorder_stmts', 'remove_ast', 'remove_var', 'add_mask', 'cast_simple',
          'randomize_internal_type', 'factor_mult', 'duplicate_assignment',
          'split_assignment'}   # iter 107: split `spr_y -= 2` into `spr_y = 2; spr_y = spr_y - spr_y;`
NEVER = {'randomize_external_type', 'randomize_function_type', 'inline', 'float_literal', 'sameline'}
RISKY = UNSAFE | {'temp_for_expr', 'expand_expr', 'reorder_decls'}
BASE = {   # decomp-permuter default_weights.toml [base] at 860b6d9
    'temp_for_expr': 100, 'expand_expr': 20, 'reorder_stmts': 10, 'reorder_decls': 10,
    'add_mask': 15, 'xor_zero': 10, 'cast_simple': 10, 'refer_to_var': 10, 'float_literal': 3,
    'randomize_internal_type': 10, 'randomize_external_type': 5, 'randomize_function_type': 5,
    'split_assignment': 10, 'sameline': 3, 'ins_block': 10, 'struct_ref': 10, 'empty_stmt': 10,
    'condition': 10, 'mult_zero': 5, 'factor_mult': 5, 'factor_shift': 5, 'dummy_comma_expr': 5,
    'add_self_assignment': 5, 'commutative': 5, 'add_sub': 5, 'inequalities': 5,
    'compound_assignment': 5, 'remove_ast': 5, 'duplicate_assignment': 5, 'chain_assignment': 5,
    'long_chain_assignment': 3, 'pad_var_decl': 1, 'inline': 10, 'var_cond_block': 5,
    'alias_array': 1, 'remove_var': 5, 'reverse_cond': 1}
CPP_DEFS = ['-D__386__', '-D__WATCOMC__=950', '-DM_I386', '-D_M_I386', '-D__FLAT__',
            '-D_M_IX86=300', '-D__X86__'] + ['-D%s=' % k for k in (
            '__far', '__near', '__interrupt', '__huge', '_far', '_near', '__cdecl', '__pascal',
            '__loadds', '__saveregs')]

args = sys.argv[1:]
check = None
reduce = None
if '--reduce' in args:
    i = args.index('--reduce'); reduce = args[i + 1]; del args[i:i + 2]
if '--check' in args:
    i = args.index('--check'); check = args[i + 1]; del args[i:i + 2]
flags = {'--all', '--weighted', '--hill'}
opt = {a for a in args if a in flags}
args = [a for a in args if a not in flags]
slack = only = None
if '--slack' in args:
    i = args.index('--slack'); slack = int(args[i + 1]); del args[i:i + 2]
if '--only' in args:
    i = args.index('--only'); only = set(args[i + 1].split(',')); del args[i:i + 2]
if check:
    args = ['-', '0', '0']
name, addr, endg = args[0], int(args[1], 16), int(args[2], 16)
rounds = int(args[3]) if len(args) > 3 else 10
copies = int(args[4]) if len(args) > 4 else 30
rng = random.Random(int(args[5]) if len(args) > 5 else 1)

if '--all' not in opt:
    # iter 107: temp_for_expr reused the live `crc` of net_recv as its temporary (`crc = buf[4];`
    # before `if (crc != rcrc)`): its liveness test goes by text position. New temporaries only.
    R.PROB_REUSE_VAR = 0
weights = {}
for m in R.RANDOMIZATION_PASSES:
    p = m.__name__[5:]
    w = BASE.get(p, 0)
    if p in NEVER or (p in UNSAFE and '--all' not in opt) or (only is not None and p not in only):
        w = 0
    if w:
        weights[m] = w
if not weights:
    sys.exit('no passes left (check --only names: %s)' % ', '.join(sorted(BASE)))
methods = list(weights.items())

# ---- parse src/ww.c (Watcom's headers under lower-case names for cpp)
hdir = os.path.join(ROOT, '.work', 'astperm', 'h')
os.makedirs(hdir, exist_ok=True)
wh = os.path.join(ROOT, '.work', 'toolchains', 'inst', 'WC95B', 'H')
for f in os.listdir(wh):
    dst = os.path.join(hdir, f.lower())
    if f.upper().endswith('.H') and not os.path.exists(dst):
        os.symlink(os.path.join(wh, f), dst)
pre = subprocess.run(['cpp', '-nostdinc', '-I', hdir, '-P'] + CPP_DEFS + [os.path.join(ROOT, 'src', 'ww.c')],
                     capture_output=True, text=True)
if pre.returncode:
    sys.exit('cpp failed:\n' + pre.stderr[-2000:])
ast = ast_util.parse_c(pre.stdout)

if check:
    # Every listed function's body printed from the tree.
    ts, fails = [], []
    for line in open(check):
        n, sz, a = line.split()[:3]
        n = n.rstrip('_')
        try:
            t = permlib.Target(n, int(a, 16), int(a, 16) + int(sz), workdir='perm')
            f = next((x for x in ast.ext if isinstance(x, ca.FuncDef) and x.decl.name == t.cname), None)
            if f is None:
                raise Exception('%s not in the parsed tree' % t.cname)
        except (SystemExit, Exception) as e:
            fails.append('%s (%s)' % (n, e)); continue
        ast_util.normalize_ast(f, ast)
        body = t.head + ast_util.to_c(f.body).rstrip() + '\n'
        ts.append((t, body))
    # In place: each listed body replaced by its print, against a build of src/ww.c as it is (a copy
    # placed next to the original would change both their neighbours: permlib.compile_round).
    base = ts[0][0].src
    probe = base
    for t, body in sorted(ts, key=lambda x: -x[0].fend):
        probe = probe[:t.fstart] + body + '\n' + probe[t.fend:]
    work = os.path.join(ROOT, '.work', 'astperm')
    objs = {}
    for tag, text in (('BASE', base), ('RT', probe)):
        open(os.path.join(work, tag + '.C'), 'w').write(text)
        obj = os.path.join(work, tag + '.OBJ')
        if os.path.exists(obj):
            os.remove(obj)
        subprocess.run([os.path.join(ROOT, 'tools', 'wc', 'wcc.sh'), os.path.join(ROOT, '.work', 'toolchains', 'inst', 'WC95B'),
                        work, tag + '.C', '-s', '-3r', '-oneatx'], capture_output=True, text=True)
        if not os.path.exists(obj):
            sys.exit('%s probe did not compile: %s/WCC.OUT' % (tag, work))
        objs[tag] = obj
    same = 0
    for t, _ in ts:
        o = t.score_obj(objs['BASE'], [t.name]).get(t.name)
        r = t.score_obj(objs['RT'], [t.name]).get(t.name)
        ok = o is not None and o == r
        same += ok
        print('%-22s original %-10s printed %-10s %s' % (t.name, o, r, 'same' if ok else 'DIFFERS'))
    for f in fails:
        print('not parsed: %s' % f)
    print('round trip: %d of %d the same, %d not found' % (same, len(ts), len(fails)))
    sys.exit(0)

T = permlib.Target(name, addr, endg, workdir='perm_ast')   # apart from permute.py's .work/perm
fn0, fn_index = ast_util.extract_fn(ast, T.cname)
ast_util.normalize_ast(fn0, ast)


def text_of(fn):
    return T.head + ast_util.to_c(fn.body).rstrip() + '\n'


def mutate(fn):
    new = copy.deepcopy(fn)
    a = copy.copy(ast)
    a.ext = copy.copy(ast.ext)
    a.ext[fn_index] = new
    labs = []
    for _ in range(rng.choice([1, 1, 1, 2, 2, 3])):
        for _ in range(20):
            m = R.random_weighted(rng, methods)
            indices = ast_util.compute_node_indices(new)
            region = R.get_randomization_region(new, indices, rng)
            try:
                m(new, a, indices, region, rng)
            except R.RandomizationFailure:
                continue
            p = m.__name__[5:]
            labs.append(('!' if p in RISKY else '') + p)
            break
    if not labs:
        return None, []
    return new, labs


start = text_of(fn0)


def inplace(text):
    """Rows and weight of NAME with its definition replaced by TEXT in src/ww.c (one compile)."""
    work = T.work
    open(os.path.join(work, 'IP.C'), 'w').write(T.src[:T.fstart] + text + T.src[T.fend:])
    obj = os.path.join(work, 'IP.OBJ')
    if os.path.exists(obj):
        os.remove(obj)
    subprocess.run([os.path.join(ROOT, 'tools', 'wc', 'wcc.sh'), os.path.join(ROOT, '.work', 'toolchains', 'inst', 'WC95B'),
                    work, 'IP.C', '-s', '-3r', '-oneatx'], capture_output=True, text=True)
    if not os.path.exists(obj):
        return None
    return T.score_obj(obj, [T.cname]).get(T.cname) or T.score_obj(obj, [name]).get(name)


if reduce:
    import difflib
    win = open(reduce).read()
    if win.startswith('/*'):
        win = win.split('\n', 1)[1]
    a, b = start.split('\n'), win.split('\n')
    hunks = [op for op in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes() if op[0] != 'equal']

    def build(keep):
        out, last = [], 0
        for k, (tag, i1, i2, j1, j2) in enumerate(hunks):
            out += a[last:i1]
            out += b[j1:j2] if k in keep else a[i1:i2]
            last = i2
        return '\n'.join(out + a[last:])

    keep = set(range(len(hunks)))
    cur = inplace(build(keep))
    print('in place: printed original %s, winner %s, %d hunks' % (inplace(start), cur, len(hunks)), flush=True)
    changed = True
    while changed:              # again until stable: a declaration is needed until its uses go
        changed = False
        for k in sorted(keep):
            trial = keep - {k}
            v = inplace(build(trial))
            tag, i1, i2, j1, j2 = hunks[k]
            verdict = 'undone' if v is not None and v[0] <= cur[0] else 'needed'
            print('hunk %2d %-8s %s  -> %s' % (k, verdict, ' / '.join(x.strip() for x in b[j1:j2])[:90], v), flush=True)
            if verdict == 'undone':
                keep, cur, changed = trial, v, True
    out = os.path.join(T.work, 'reduced_%s_%d.c' % (name, cur[0]))
    open(out, 'w').write(build(keep))
    print('reduced: %d of %d hunks needed, %s -> %s' % (len(keep), len(hunks), cur, out))
    print('READ IT: a pass can change behaviour (iter 107: net_recv, overlay_draw), and so can undoing'
          ' some hunks and keeping others')
    # A permuter temp that is read but never assigned is an uninitialized read (iter 108: in_sector's
    # `tbl6e858[new_var2]` after its assignment hunk was undone; fn_1bf00's `nplayers = n`).
    red = build(keep)
    for v in sorted(set(re.findall(r'\b(new_var\d*)\b', red))):
        if not re.search(r'\b%s\s*=[^=]' % v, red):
            print('UNINITIALIZED: %s is read but never assigned - reject' % v)
    for k in sorted(keep):
        tag, i1, i2, j1, j2 = hunks[k]
        print('  - %s\n  + %s' % (' / '.join(x.strip() for x in a[i1:i2]), ' / '.join(x.strip() for x in b[j1:j2])))
    sys.exit(0)
sc = T.compile_round([(T.func, []), (start, ['roundtrip'])])
if not sc or 'pm001' not in sc:
    sys.exit('round-trip compile failed (see %s/WCC.OUT and PERM.C)' % T.work)
orig, rt = sc['pm000'], sc['pm001']
print('control %s: %d rows, weight %d; printed from the tree: %d rows, weight %d%s'
      % (name, orig[0], orig[1], rt[0], rt[1],
         '' if rt == orig else '  ** THE ROUND TRIP CHANGED THE CODE: scores are against the printed body **'),
      flush=True)
print('passes: %s' % ', '.join('%s=%g' % (m.__name__[5:], w) for m, w in methods), flush=True)
best_rows, best_l = permlib.walk(T, fn0, text_of, mutate, rounds, copies, rng,
                                 weighted='--weighted' in opt, slack=slack, hill='--hill' in opt,
                                 control=rt, best_prefix='ast')
print('final best %d: %s' % (best_rows, '; '.join(best_l)))

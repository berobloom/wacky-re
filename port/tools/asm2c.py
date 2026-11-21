#!/usr/bin/env python3
"""asm2c.py: src/wwasm.asm (the game's assembler module, 0x36a4e-0x409ee) -> C for the port.

usage: port/tools/asm2c.py src/wwasm.asm OUT.c

Why translate and not run it (port/README.md): the module drives the VGA in Mode X and pokes the
hardware (in/out, cli/sti, iretd), which user mode cannot do and plain memory cannot model. The C it
writes runs the same instructions over an emulated register file (port/src/cpu.h, one per thread),
with every memory operand through rd/wr helpers that route the 0xa0000 window to the emulated VGA.
Everything else is a host address: the port is 32-bit and the game's data object is mapped at its
own address, so the module's pointers are the process's pointers.

Shape of the output: one function `run(entry)` holding every instruction, with a C label for every
asm label, so jumps between routines and fall-throughs stay gotos. `call X` pushes a dummy return
address and recurses into run at X; `ret` pops it and returns (so [esp+n] sees the layout the code
expects). Each exported routine `name_` becomes `void name(void)`: src/ww.c declares all 36 that way,
the C and the asm talk through globals only.

Flags are computed only where they are read: a flag-setting instruction gets the flag update only
if a consumer (jcc, adc, loop(n)e) can see it before another instruction sets them again; across a
label or a jump the answer is "yes". The unrolled drawing code then costs no flag work.

Never edit the output; fix this script and regenerate (port/build.sh does).
"""
import re
import sys

R32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
R16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
R8 = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh']
SIZE = {'byte': 8, 'word': 16, 'dword': 32}
CTYPE = {8: 'uint8_t', 16: 'uint16_t', 32: 'uint32_t'}
JCC = {'je': 'c->zf', 'jz': 'c->zf', 'jne': '!c->zf', 'jnz': '!c->zf',
       'jl': '(c->sf != c->of)', 'jge': '(c->sf == c->of)',
       'jle': '(c->zf || c->sf != c->of)', 'jg': '(!c->zf && c->sf == c->of)',
       'jb': 'c->cf', 'jc': 'c->cf', 'jae': '!c->cf', 'jnc': '!c->cf',
       'jbe': '(c->cf || c->zf)', 'ja': '(!c->cf && !c->zf)',
       'js': 'c->sf', 'jns': '!c->sf'}
SETTERS = {'add', 'adc', 'sub', 'sbb', 'cmp', 'and', 'or', 'xor', 'test', 'shl', 'shr', 'sar',
           'inc', 'dec', 'neg', 'mul', 'imul'}
CONSUMERS = set(JCC) | {'adc', 'sbb', 'loopne', 'loope'}


class Op:
    """one operand: reg (name, size), imm (value), mem (size, address expr), label"""

    def __init__(self, kind, size=None, val=None):
        self.kind, self.size, self.val = kind, size, val


class Line:
    def __init__(self, no, label=None, mn=None, ops=(), rep=None, text=''):
        self.no, self.label, self.mn, self.ops, self.rep, self.text = no, label, mn, list(ops), rep, text


def parse(path):
    """the instruction stream with labels; %ifdef BIN takes the %else side (the OMF build)"""
    ext, lines, skip, scope = {}, [], False, ''
    for no, raw in enumerate(open(path, encoding='latin-1'), 1):
        s = raw.split(';', 1)[0].rstrip()
        st = s.strip()
        if st.startswith('%ifdef BIN'):
            skip = True
            continue
        if st.startswith('%else'):
            skip = False
            continue
        if st.startswith('%endif'):
            skip = False
            continue
        if skip or not st:
            continue
        m = re.match(r'EXT\s+(\w+),\s*(0x[0-9a-f]+)$', st)
        if m:
            ext[m.group(1)] = int(m.group(2), 16)
            continue
        if st.startswith(('%', 'global', 'segment', 'bits', 'org', 'extern', 'REG ', 'db ')) or \
                re.match(r'^(%macro|%endmacro|%define)', st):
            continue
        m = re.match(r'^([.\w]+):\s*(.*)$', st)
        if m:
            name = m.group(1)
            if name.startswith('.'):
                name = scope + name
            else:
                scope = name
            lines.append(Line(no, label=name))
            st = m.group(2).strip()
            if not st:
                continue
        lines.append(parse_insn(no, st, scope, ext))
    return lines, ext


def parse_insn(no, st, scope, ext):
    st = re.sub(r'\b(short|near|strict)\s+', '', st)
    rep = None
    m = re.match(r'^(rep|repe|repne)\s+(.*)$', st)
    if m:
        rep, st = m.group(1), m.group(2)
    m = re.match(r'^rr\s+(\w+),\s*(\w+),\s*(\w+)$', st)          # the reg-reg encoding macro
    if m:
        st = '%s %s, %s' % m.groups()
    parts = st.split(None, 1)
    mn = parts[0]
    ops = []
    if len(parts) > 1:
        for o in split_ops(parts[1]):
            ops.append(operand(o.strip(), scope, ext, no))
    return Line(no, mn=mn, ops=ops, rep=rep, text=st)


def split_ops(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '[':
            depth += 1
        elif ch == ']':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    out.append(cur)
    return out


def num(s):
    s = s.strip()
    return int(s, 16) if s.lower().startswith(('0x', '-0x')) else int(s)


def operand(o, scope, ext, no):
    if o in R32:
        return Op('reg', 32, R32.index(o))
    if o in R16:
        return Op('reg', 16, R16.index(o))
    if o in R8:
        return Op('reg', 8, R8.index(o))
    if o in ('ds', 'es', 'ss', 'cs', 'fs', 'gs'):
        return Op('sreg', 16, o)
    m = re.match(r'^(byte|word|dword)\s*\[(.*)\]$', o)
    if m:
        return Op('mem', SIZE[m.group(1)], addr_expr(m.group(2), ext, no))
    m = re.match(r'^(byte|word|dword)\s+(-?(0x[0-9a-f]+|\d+))$', o)       # `strict word 1`
    if m:
        return Op('imm', SIZE[m.group(1)], num(m.group(2)))
    if o.startswith('seg '):
        return Op('imm', 16, 0)
    if re.match(r'^-?(0x[0-9a-f]+|\d+)$', o):
        return Op('imm', None, num(o))
    if re.match(r'^[.\w]+$', o):
        return Op('label', None, scope + o if o.startswith('.') else o)
    raise SystemExit('wwasm.asm:%d: operand %r' % (no, o))


def addr_expr(e, ext, no):
    terms = []
    for t in re.split(r'\s*\+\s*', e.strip()):
        m = re.match(r'^(\w+)\*(\d)$', t)
        if m and m.group(1) in R32:
            terms.append('c->r[%d]*%s' % (R32.index(m.group(1)), m.group(2)))
        elif t in R32:
            terms.append('c->r[%d]' % R32.index(t))
        elif t in ext:
            terms.append('0x%xu' % ext[t])
        elif re.match(r'^(0x[0-9a-f]+|\d+)$', t):
            terms.append('0x%xu' % num(t))
        else:
            raise SystemExit('wwasm.asm:%d: address term %r' % (no, t))
    return '(uint32_t)(' + ' + '.join(terms) + ')'


# ---- expressions over the register file

def get(op, size=None):
    if op.kind == 'reg':
        n = op.val
        if op.size == 32:
            return 'c->r[%d]' % n
        if op.size == 16:
            return '(uint16_t)c->r[%d]' % n
        return '(uint8_t)c->r[%d]' % n if n < 4 else '(uint8_t)(c->r[%d] >> 8)' % (n - 4)
    if op.kind == 'mem':
        return 'rd%d(%s)' % (op.size, op.val)
    if op.kind == 'imm':
        bits = size or 32
        return '0x%xu' % (op.val & ((1 << bits) - 1))
    if op.kind == 'sreg':
        return '0u'
    raise ValueError(op.kind)


def put(op, expr):
    if op.kind == 'reg':
        n = op.val
        if op.size == 32:
            return 'c->r[%d] = (uint32_t)(%s);' % (n, expr)
        if op.size == 16:
            return 'c->r[%d] = (c->r[%d] & 0xffff0000u) | (uint16_t)(%s);' % (n, n, expr)
        if n < 4:
            return 'c->r[%d] = (c->r[%d] & 0xffffff00u) | (uint8_t)(%s);' % (n, n, expr)
        return 'c->r[%d] = (c->r[%d] & 0xffff00ffu) | (uint32_t)(uint8_t)(%s) << 8;' % (n - 4, n - 4, expr)
    if op.kind == 'mem':
        return 'wr%d(%s, %s);' % (op.size, op.val, expr)
    if op.kind == 'sreg':
        return ';'
    raise ValueError(op.kind)


def opsize(ops):
    for o in ops:
        if o.kind in ('reg', 'mem') and o.size:
            return o.size
    return 32


def cname(label):
    return 'L_' + re.sub(r'\W', '_', label)


# ---- which flag updates something can read

def flags_needed(lines):
    need = [False] * len(lines)
    for i, ln in enumerate(lines):
        if ln.mn not in SETTERS:
            continue
        cf_only = False
        for j in range(i + 1, len(lines)):
            nx = lines[j]
            if nx.label is not None or nx.mn in ('jmp', 'call', 'ret', 'iretd') or nx.mn in CONSUMERS:
                need[i] = True
                break
            if nx.mn in SETTERS:
                if nx.mn in ('inc', 'dec'):     # they leave CF: a later CF reader still sees ours
                    cf_only = True
                    continue
                if cf_only and nx.mn in SETTERS:
                    break
                break
    return need


# ---- one instruction

def emit(ln, need, out):
    mn, ops, f = ln.mn, ln.ops, need
    w = out.append
    if mn in ('nop', 'cld', 'cli', 'sti'):
        return                                   # no DF=1 code; interrupts are the hardware thread's
    if mn == 'mov':
        if ops[0].kind == 'sreg' or ops[1].kind == 'sreg':
            if ops[0].kind != 'sreg':
                w(put(ops[0], '0'))
            return                               # flat model: segment registers mean nothing here
        w(put(ops[0], get(ops[1], ops[0].size)))
        return
    if mn in ('add', 'sub', 'and', 'or', 'xor', 'adc', 'cmp', 'test'):
        a, b = ops
        sz = opsize(ops)
        ea, eb = get(a, sz), get(b, sz)
        t = CTYPE[sz]
        if mn == 'xor' and a.kind == 'reg' and b.kind == 'reg' and a.val == b.val and a.size == b.size:
            w(put(a, '0'))
            if f:
                w('c->zf = 1; c->sf = 0; c->cf = 0; c->of = 0;')
            return
        if mn in ('cmp', 'test'):
            if f and mn == 'cmp':
                w('flags_sub%d(c, %s, %s);' % (sz, ea, eb))
            elif f:
                w('flags_logic%d(c, (%s)((%s) & (%s)));' % (sz, t, ea, eb))
            return
        if f or mn == 'adc':
            fn = {'add': 'add', 'sub': 'sub', 'adc': 'adc', 'and': 'and', 'or': 'or', 'xor': 'xor'}[mn]
            w('{ %s v = op_%s%d(c, %s, %s); %s }' % (t, fn, sz, ea, eb, put(a, 'v')))
            if not f and mn == 'adc':
                pass
            return
        sym = {'add': '+', 'sub': '-', 'and': '&', 'or': '|', 'xor': '^'}[mn]
        w(put(a, '(%s)(%s %s %s)' % (t, ea, sym, eb)))
        return
    if mn in ('inc', 'dec'):
        a = ops[0]
        sz = a.size
        if f:
            w('{ %s v = op_%s%d(c, %s); %s }' % (CTYPE[sz], mn, sz, get(a), put(a, 'v')))
        else:
            w(put(a, '(%s)(%s %s 1)' % (CTYPE[sz], get(a), '+' if mn == 'inc' else '-')))
        return
    if mn in ('shl', 'shr', 'sar'):
        a, b = ops
        sz = a.size
        cnt = get(b, 8) + ' & 31' if b.kind != 'imm' else str(b.val & 31)
        if f:
            w('{ %s v = op_%s%d(c, %s, %s); %s }' % (CTYPE[sz], mn, sz, get(a), cnt, put(a, 'v')))
        elif mn == 'sar':
            w(put(a, '(%s)((int%d_t)%s >> (%s))' % (CTYPE[sz], sz, get(a), cnt)))
        else:
            w(put(a, '(%s)(%s %s (%s))' % (CTYPE[sz], get(a), '<<' if mn == 'shl' else '>>', cnt)))
        return
    if mn in ('mul', 'imul') and len(ops) == 1:
        a = ops[0]
        sz = a.size
        if sz == 32:
            if mn == 'mul':
                w('{ uint64_t p = (uint64_t)c->r[0] * %s; c->r[0] = (uint32_t)p; c->r[2] = (uint32_t)(p >> 32);'
                  ' c->cf = c->of = c->r[2] != 0; }' % get(a))
            else:
                w('{ int64_t p = (int64_t)(int32_t)c->r[0] * (int32_t)%s; c->r[0] = (uint32_t)p;'
                  ' c->r[2] = (uint32_t)((uint64_t)p >> 32); c->cf = c->of = p != (int32_t)p; }' % get(a))
        elif sz == 16:
            w('{ uint32_t p = %s(%s)(uint16_t)c->r[0] * (%s)%s; %s %s c->cf = c->of = (p >> 16) != %s; }' % (
                '(uint32_t)' if mn == 'mul' else '(uint32_t)(int32_t)',
                'uint32_t' if mn == 'mul' else 'int16_t', 'uint32_t' if mn == 'mul' else 'int16_t', get(a),
                put(Op('reg', 16, 0), 'p'), put(Op('reg', 16, 2), 'p >> 16'),
                '0' if mn == 'mul' else '((p & 0x8000) ? 0xffffu : 0)'))
        else:
            w('{ uint16_t p = %s; %s c->cf = c->of = (p >> 8) != 0; }' % (
                '(uint16_t)((uint8_t)c->r[0] * (uint8_t)%s)' % get(a) if mn == 'mul'
                else '(uint16_t)((int8_t)c->r[0] * (int8_t)%s)' % get(a), put(Op('reg', 16, 0), 'p')))
        return
    if mn == 'idiv' and ops[0].size == 16:
        w('{ int32_t n = (int32_t)((uint32_t)(uint16_t)c->r[2] << 16 | (uint16_t)c->r[0]); int16_t d = (int16_t)%s;'
          ' if (!d) port_fatal("asm: divide by zero at wwasm.asm:%d"); %s %s }' % (
              get(ops[0]), ln.no, put(Op('reg', 16, 0), 'n / d'), put(Op('reg', 16, 2), 'n % d')))
        return
    if mn == 'cwd':
        w(put(Op('reg', 16, 2), '(c->r[0] & 0x8000) ? 0xffffu : 0'))
        return
    if mn == 'xchg':
        a, b = ops
        w('{ %s t = %s; %s %s }' % (CTYPE[a.size], get(a), put(a, get(b)), put(b, 't')))
        return
    if mn == 'push':
        a = ops[0]
        if a.kind == 'sreg':
            w('push32(c, 0);')
        elif a.kind == 'reg' and a.size == 16:
            w('push16(c, %s);' % get(a))
        else:
            w('push32(c, %s);' % get(a))
        return
    if mn == 'pop':
        a = ops[0]
        if a.kind == 'sreg':
            w('c->r[4] += 4;')
        elif a.kind == 'reg' and a.size == 16:
            w(put(a, 'pop16(c)'))
        else:
            w(put(a, 'pop32(c)'))
        return
    if mn == 'pushad':
        w('{ uint32_t s = c->r[4]; push32(c, c->r[0]); push32(c, c->r[1]); push32(c, c->r[2]); push32(c, c->r[3]);'
          ' push32(c, s); push32(c, c->r[5]); push32(c, c->r[6]); push32(c, c->r[7]); }')
        return
    if mn == 'popad':
        w('{ c->r[7] = pop32(c); c->r[6] = pop32(c); c->r[5] = pop32(c); c->r[4] += 4;'
          ' c->r[3] = pop32(c); c->r[2] = pop32(c); c->r[1] = pop32(c); c->r[0] = pop32(c); }')
        return
    if mn == 'pushaw':
        w('{ uint16_t s = (uint16_t)c->r[4]; for (int k = 0; k < 8; k++) push16(c, k == 4 ? s : (uint16_t)c->r[k]); }')
        return
    if mn == 'popaw':
        w('{ for (int k = 7; k >= 0; k--) { uint16_t v = pop16(c);'
          ' if (k != 4) c->r[k] = (c->r[k] & 0xffff0000u) | v; } }')
        return
    if mn == 'in':
        a, b = ops
        port = get(b, 16) if b.kind == 'reg' else '0x%xu' % b.val
        if a.size == 8:
            w(put(a, 'port_in8(%s)' % port))
        else:
            w(put(a, 'port_in16(%s)' % port))
        return
    if mn == 'out':
        a, b = ops
        port = get(a, 16) if a.kind == 'reg' else '0x%xu' % a.val
        if b.size == 8:
            w('port_out8(%s, %s);' % (port, get(b)))
        else:
            w('port_out16(%s, (uint16_t)c->r[0]);' % port)
        return
    if mn in ('movsb', 'movsw', 'movsd', 'stosb', 'stosw', 'stosd', 'lodsb'):
        sz = {'b': 8, 'w': 16, 'd': 32}[mn[-1]]
        step = sz // 8
        body = {'movs': 'wr%d(c->r[7], rd%d(c->r[6])); c->r[6] += %d; c->r[7] += %d;' % (sz, sz, step, step),
                'stos': 'wr%d(c->r[7], %s); c->r[7] += %d;' % (sz, get(Op('reg', sz, 0)), step),
                'lods': '%s c->r[6] += %d;' % (put(Op('reg', sz, 0), 'rd%d(c->r[6])' % sz), step)}[mn[:4]]
        if ln.rep:
            if mn.startswith('movs') and sz == 8:
                w('rep_movsb(c);')
            else:
                w('while (c->r[1]) { %s c->r[1]--; }' % body)
        else:
            w(body)
        return
    if mn == 'jmp':
        w('goto %s;' % cname(ops[0].val))
        return
    if mn in JCC:
        w('if (%s) goto %s;' % (JCC[mn], cname(ops[0].val)))
        return
    if mn == 'loop':
        w('if (--c->r[1]) goto %s;' % cname(ops[0].val))
        return
    if mn == 'loopne':
        w('if (--c->r[1] && !c->zf) goto %s;' % cname(ops[0].val))
        return
    if mn == 'call':
        w('push32(c, 0xc0de0000u); run(E_%s);' % cname(ops[0].val))
        return
    if mn == 'ret':
        w('c->r[4] += 4; return;')
        return
    if mn == 'iretd':
        w('c->r[4] += 12; return;')
        return
    raise SystemExit('wwasm.asm:%d: no translation for %r' % (ln.no, ln.text))


def main(src, out):
    lines, _ = parse(src)
    need = flags_needed(lines)
    exports = []
    for raw in open(src, encoding='latin-1'):
        m = re.match(r'^\s+global\s+(\w+)', raw)
        if m:
            exports.append(m.group(1))
    labels = [ln.label for ln in lines if ln.label is not None]
    entries = list(dict.fromkeys(exports + [ln.ops[0].val for ln in lines if ln.mn == 'call']))
    for e in entries:
        if e not in labels:
            raise SystemExit('entry %s has no label' % e)
    o = ['/* generated by port/tools/asm2c.py from %s - do not edit */' % src,
         '#include "cpu.h"', '',
         'enum { %s };' % ', '.join('E_%s' % cname(e) for e in entries),
         'static void run(int entry);', '']
    for e in exports:
        isr = e == 'kbd_isr_'
        o.append('void %s(void)\n{\n    struct cpu *c = cpu_enter();\n    uint32_t sp = c->r[4];\n' % e[:-1])
        if isr:
            o.append('    push32(c, 0); push32(c, 0); push32(c, 0xc0de0000u);   /* EFLAGS, CS, EIP for iretd */')
        else:
            o.append('    push32(c, 0xc0de0000u);')
        o.append('    run(E_%s);\n    if (c->r[4] != sp)\n        port_fatal("asm %s: stack off by %%d", (int)(c->r[4] - sp));\n    cpu_leave();\n}\n'
                 % (cname(e), e[:-1]))
    o.append('static void run(int entry)\n{\n    struct cpu *c = cpu_get();\n')
    o.append('    switch (entry) {')
    for e in entries:
        o.append('    case E_%s: goto %s;' % (cname(e), cname(e)))
    o.append('    }\n    return;')
    body = []
    for i, ln in enumerate(lines):
        if ln.label is not None:
            body.append('%s:;' % cname(ln.label))
            continue
        code = []
        emit(ln, need[i], code)
        if code:
            body.append('#line %d "src/wwasm.asm"' % ln.no)
            body.extend('    ' + x for x in code)
    o.extend(body)
    o.append('    port_fatal("asm: ran off the end of wwasm.asm");\n}')
    open(out, 'w').write('\n'.join(o) + '\n')
    nflag = sum(1 for i, ln in enumerate(lines) if ln.mn in SETTERS and need[i])
    nset = sum(1 for ln in lines if ln.mn in SETTERS)
    print('asm2c: %d instructions, %d exports, %d entries, flags kept on %d of %d setters'
          % (sum(1 for ln in lines if ln.mn), len(exports), len(entries), nflag, nset), file=sys.stderr)


if __name__ == '__main__':
    main(*sys.argv[1:3])

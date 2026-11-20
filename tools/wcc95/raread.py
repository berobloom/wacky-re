#!/usr/bin/env python3
"""raread.py: print a ratrace.py log per allocation pass: allocation order, savings, the C name
(from SYM=NAME arguments, e.g. syma59=crc), the register given, and GiveBestReg's candidates
(register: TooGreedy result / CountRegMoves score; `x` = excluded by a conflict or the except set).

usage: tools/wcc95/raread.py LOG [FUNC] [symHANDLE=NAME ...]
"""
import re
import sys

args = sys.argv[2:]
func = args.pop(0) if args and '=' not in args[0] else None
names = dict(x.split('=') for x in args)
passes, cur, conf = [], None, {}
for l in open(sys.argv[1]):
    m = re.match(r'PRESORT (\S+) line (\d+) n (\d+)', l)
    if m:
        cur = {'fn': m.group(1), 'line': m.group(2), 'pre': [], 'order': []}
        passes.append(cur)
        continue
    if cur is None:
        continue
    m = re.match(r'\s+c([0-9a-f]+) s(\d+) id(\d+) sym([0-9a-f]+) cls(\d+)', l)
    if m and not cur['order']:
        cur['pre'].append(m.group(1))
        continue
    m = re.match(r'ASSIGN c([0-9a-f]+) s(\d+) st([0-9a-f]+) id(\d+) sym([0-9a-f]+) cls(\d+)', l)
    if m:
        conf = {'c': m.group(1), 's': int(m.group(2)), 'id': m.group(4), 'sym': m.group(5),
                'cls': m.group(6), 'reg': 'mem', 'cands': []}
        cur['order'].append(conf)
        continue
    m = re.match(r'\s+cand c([0-9a-f]+) (\S+)\s+with (\S+) except (\S+) given (\S+)', l)
    if m and conf:
        conf['cands'].append([m.group(2), None, None, m.group(3), m.group(4)])
        continue
    m = re.match(r'\s+greedy (\d+)', l)
    if m and conf and conf['cands']:
        conf['cands'][-1][1] = int(m.group(1))
        continue
    m = re.match(r'\s+saves (-?\d+)', l)
    if m and conf and conf['cands']:
        conf['cands'][-1][2] = int(m.group(1))
        continue
    m = re.match(r'GIVEN  c([0-9a-f]+) (\S+)', l)
    if m:
        for o in cur['order']:
            if o['c'] == m.group(1):
                o['reg'] = m.group(2)

for p in passes:
    if func and p['fn'] != func:
        continue
    print('%s pass at line %s: %d conflicts' % (p['fn'], p['line'], len(p['pre'])))
    pos = {c: i for i, c in enumerate(p['pre'])}
    for o in p['order']:
        if o['cls'] != '2':
            continue
        nm = names.get('sym' + o['sym'], 'sym' + o['sym'] if o['sym'] != '0' else '')
        tie = sum(1 for q in p['order'] if q['s'] == o['s']) > 1
        cs = []
        for reg, greed, saves, *_ in o['cands']:
            if greed is None:
                v = 'x'                                    # excluded before TooGreedy
            elif greed == 1:
                v = 'greedy'
            elif greed != 0:
                v = 'maybe'
            else:
                v = str(saves)
            cs.append('%s:%s' % (reg, v))
        print('   %-5s s%-6d id%-3s %-9s pre#%-3d %-3s %s' % (o['reg'], o['s'], o['id'], nm,
              pos.get(o['c'], -1), 'TIE' if tie else '', ' '.join(cs)))

"""raread.py: print a ratrace log per pass: allocation order, savings, name, register given (or memory)."""
import re, sys
names = dict(x.split('=') for x in sys.argv[2:])          # syma59=crc ...
cur = None; passes = []
for l in open(sys.argv[1]):
    if l.startswith('PRESORT'):
        passes.append({'hdr': l.strip(), 'pre': [], 'order': []}); cur = passes[-1]; continue
    m = re.match(r'\s+c([0-9a-f]+) s(\d+) id(\d+) sym([0-9a-f]+) cls(\d+)', l)
    if m and cur is not None and not cur['order']:
        cur['pre'].append(m.groups()); continue
    m = re.match(r'ASSIGN c([0-9a-f]+) s(\d+) st([0-9a-f]+) id(\d+) sym([0-9a-f]+) cls(\d+)', l)
    if m: cur['order'].append([m.group(1), int(m.group(2)), m.group(4), m.group(5), m.group(6), 'mem']); continue
    m = re.match(r'GIVEN  c([0-9a-f]+) (\S+)', l)
    if m:
        for o in cur['order']:
            if o[0] == m.group(1): o[5] = m.group(2)
for p in passes:
    print(p['hdr'])
    pos = {c: i for i, (c, *_ ) in enumerate(p['pre'])}
    for c, s, i, sym, cls, reg in p['order']:
        if cls != '2': continue
        nm = names.get('sym' + sym, '') if sym != '0' else ''
        print('   %-6s s%-6d id%-3s %-10s pre#%-3d %s' % (reg, s, i, nm, pos.get(c, -1), 'TIE' if sum(1 for q in p['order'] if q[1] == s) > 1 else ''))

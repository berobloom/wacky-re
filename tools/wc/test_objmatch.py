#!/usr/bin/env python3
"""test_objmatch.py: objmatch's rel32-by-target rule (iter 74) accepts the three layout-only near
misses and rejects a branch that lands anywhere else. Needs .work/src/WW.OBJ (src/check.sh) and
capstone; skipped without them."""
import json
import os
import struct
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import capstone                                                     # noqa: F401,E402
    import libmatch as L                                                # noqa: E402
    import objmatch as M                                                # noqa: E402
except ImportError:
    print('test_objmatch skipped (no capstone)')
    sys.exit(0)
obj = os.path.join(L.ROOT, '.work', 'src', 'WW.OBJ')
if not os.path.exists(obj):
    print('test_objmatch skipped (run src/check.sh first)')
    sys.exit(0)

le = os.path.join(L.ROOT, '.work', 'le')
code = open(os.path.join(le, 'ww.obj1.bin'), 'rb').read()
cbase = json.load(open(os.path.join(le, 'ww.json')))['objects'][0]['base']
(_, recs), = list(L.modules(open(obj, 'rb').read()))
segs = L.code_of(recs)
pubs = sorted(p for p in M.publics(recs) if p[0] in segs)
known = M.names('ww')
addr = {nm: known.get(M.plain(nm)) for _, _, nm in pubs}


def check(name, patch=None):
    i = [p[2] for p in pubs].index(name)
    seg, off, _ = pubs[i]
    buf, mask = segs[seg]
    end = next((q[1] for q in pubs[i + 1:] if q[0] == seg and q[1] > off), len(buf))
    b, m = bytearray(buf[off:end]), mask[off:end]
    if patch:
        patch(b)
    return M.rel32_by_target(b, m, code, addr[name] - cbase, cbase, seg, off, pubs, addr.get)


# the three functions whose only differing bytes are rel32 displacements (iter 65 checked by hand)
assert check('fade_out_') == 4
assert check('demo_rec_') == 1
assert check('pic_show_') == 2


def bump_first_rel32(b):
    """move pic_show's first differing jmp/jcc rel32 4 bytes further: same function, wrong offset"""
    i = [p[2] for p in pubs].index('pic_show_')
    a = addr['pic_show_'] - cbase
    for k in range(len(b) - 5):
        if (b[k] == 0xe9 or (b[k] == 0x0f and 0x80 <= b[k + 1] <= 0x8f)) and k > 0:
            p = k + (1 if b[k] == 0xe9 else 2)
            if bytes(b[p:p + 4]) != code[a + p:a + p + 4]:
                struct.pack_into('<i', b, p, struct.unpack_from('<i', b, p)[0] + 4)
                return
    raise AssertionError('no differing rel32 in pic_show (layout changed?) %d' % i)


assert check('pic_show_', bump_first_rel32) is None
# a function with a real instruction difference is never rescued (enter_name: stack slots)
assert check('enter_name_') is None
print('test_objmatch ok')

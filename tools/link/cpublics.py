#!/usr/bin/env python3
"""cpublics.py OBJ... > cpublics.txt: each code public of our C objects with its game address
(analysis/ww_names.txt), `name 0xADDR`; delink.py turns relative calls from the library code into
the C module into fixups to these names."""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'wc'))
import libmatch as L                    # noqa: E402
import objmatch as M                    # noqa: E402

names = M.names('ww')
for obj in sys.argv[1:]:
    (_, recs), = list(L.modules(open(obj, 'rb').read()))
    segs = L.code_of(recs)
    for seg, off, n in M.publics(recs):
        if seg in segs and M.plain(n) in names:
            print('%s 0x%06x' % (n, names[M.plain(n)]))

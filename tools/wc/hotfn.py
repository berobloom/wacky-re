#!/usr/bin/env python3
"""hotfn.py: which loop does a capped `tools/wc/difffn.py` run sit in? Step (b) of a near-miss night:
it runs ONE function in the ORIGINAL image under Unicorn, counts executed addresses, and prints the
hottest ones with the backward jump marked - the loop head and the global it tests are the seed.

    FN=com_wait_byte .work/venv/bin/python tools/wc/hotfn.py
    FN=classes_menu N=200000 SEED=0x7e720=0x1000 TICK=8 ...      # difffn's env vars apply
    FN=classes_menu MAP=.work/link/WWORIG.MAP NEW=wworig         # the other image

It reuses difffn.Run, so SEED/TICK/NEW/MAP mean the same thing here. Only the original's addresses are
named; for the hybrid, read the printed address against `.work/le/wwnew.json`.
"""
import collections
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'le'))
import capstone                                                    # noqa: E402
from unicorn import UC_HOOK_CODE                                   # noqa: E402
import difffn as D                                                 # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
FN = os.environ.get('FN', 'classes_menu')
N = int(os.environ.get('N', '200000'))
names = D.M.names('ww')
if FN not in names:
    sys.exit('%s not in the ww names' % FN)
r = D.Run('ww', D.CMOD_END, 1 << 32)
r.fread = D.fread_addr('ww')          # so FREAD= works here too (difffn.py sets this in main())
r.rand = D.rand_addr('ww')            # ... and RAND=
cnt = collections.Counter()


def hook(uc, a, _sz, _u):
    cnt[a] += 1
    if sum(cnt.values()) > N:
        uc.emu_stop()


r.e.uc.hook_add(UC_HOOK_CODE, hook, begin=0x10000, end=D.CMOD_END)
r.go(names[FN], count=N * 4)
print('%s: %d insns at %d addrs; %s' % (FN, sum(cnt.values()), len(cnt),
                                        r.crash or ('returned' if r.done else 'capped')))
for a, c in cnt.most_common(12):
    ins = next(md.disasm(bytes(r.e.uc.mem_read(a, 12)), a), None)
    tgt = ''
    try:
        v = int(ins.op_str.split()[-1], 16)
        if 0x10000 <= v < a:
            tgt = '  <- loops back to %#x %s' % (v, names.get(v, ''))
    except Exception:
        pass
    print('  %#08x x%-8d %s %s%s' % (a, c, ins.mnemonic, ins.op_str, tgt))

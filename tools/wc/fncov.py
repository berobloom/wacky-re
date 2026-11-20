#!/usr/bin/env python3
"""fncov.py: how much of one function do several difffn runs actually walk?

    FN=leaderboard LO=0x2b270 HI=0x2b558 .work/venv/bin/python tools/wc/fncov.py \
        "0x8000,0x8010" "0x8000,0x8010|0x88c40=6:w" "0x88b78=3:w,0x79a9c=1:w" ...

One argument per run, in the difffn env's syntax: a string with no `=` is `ARGS=`, one with `=` is
`SEED=`, and `ARGS|SEED` sets both. Always the game image `ww` - this asks "did our verdict cover that
branch?", which is a question about the game's code. Each run gets its own process because Unicorn
leaks its translator buffer per VM (six in one process: `Could not allocate dynamic translator
buffer`, iter 87). A `SAME` from a run that never reached a branch is not a verdict on that branch:
iter 87 found `kart_bump` 0x22bf4 at 489 of 606 instruction starts and `leaderboard` 0x2b270 at
211 of 215 over its seeded runs, and the four left in `leaderboard` are unreachable because the
`itoa` 0x425f9 they wait for is a stubbed clib call (findings/game-loop.md).
"""
import collections
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'le'))
FN, LO, HI = os.environ['FN'], int(os.environ['LO'], 0), int(os.environ['HI'], 0)
N = int(os.environ.get('N', '400000'))


def one(seed_str):
    """run one function call in a fresh image; return its executed addresses, counts and stop reason"""
    import difffn as D                                        # noqa: E402
    from unicorn import UC_HOOK_CODE                          # noqa: E402
    args, bar, seed = seed_str.partition('|')
    if bar:
        D.SEED, D.ARGREGS = D.parse_seed(seed), args.split(',')
    elif '=' in seed_str:
        D.SEED, D.ARGREGS = D.parse_seed(seed_str), ['']
    else:
        D.SEED, D.ARGREGS = [], seed_str.split(',')
    cnt = collections.Counter()
    r = D.Run('ww', D.CMOD_END, 1 << 32)
    r.fread, r.rand = D.fread_addr('ww'), D.rand_addr('ww')
    r.getch, r.exit = D.getch_addr('ww'), D.exit_addr('ww')

    def hook(uc, addr, _sz, _u):
        cnt[addr] += 1
        if sum(cnt.values()) > N:
            uc.emu_stop()

    r.e.uc.hook_add(UC_HOOK_CODE, hook, begin=0x10000, end=D.CMOD_END)
    r.go(D.M.names('ww')[FN], count=N * 4)
    return {'n': sum(cnt.values()), 'at': len(cnt), 'stop': r.crash or ('returned' if r.done else 'capped'),
            'in': len([a for a in cnt if LO <= a < HI]), 'addrs': sorted(cnt)}


def main():
    if os.environ.get('FNCOV_ONE'):            # child: one run, JSON out (Unicorn leaks a VM each)
        print(json.dumps(one(sys.argv[1])))
        return
    import capstone                                            # noqa: E402
    from lelib import Image                                    # noqa: E402
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True          # else the decode stops silently at the first filler byte
    starts = [i.address for i in md.disasm(Image('ww').read(LO, HI - LO), LO)]
    cov = set()
    for s in sys.argv[1:]:
        out = subprocess.run([sys.executable, os.path.abspath(__file__), s],
                             env=dict(os.environ, FNCOV_ONE='1'), capture_output=True, text=True)
        d = json.loads(out.stdout)
        cov |= {a for a in d['addrs'] if LO <= a < HI}
        print('%s: %d insns at %d addrs (%d in range); %s'
              % (s[:40] + '...', d['n'], d['at'], d['in'], d['stop']))
    miss = [a for a in starts if a not in cov]
    print('union %d addrs in %#x-%#x; %d of %d instruction starts never executed'
          % (len(cov), LO, HI, len(miss), len(starts)))
    print(' '.join('%05x' % a for a in miss)[:6000])


if __name__ == '__main__':
    main()

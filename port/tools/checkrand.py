#!/usr/bin/env python3
"""checkrand.py: the port's rand/srand (port/src/wrand.c) against WW.EXE's own (0x43088, 0x430ac)
under Unicorn, for a set of seeds and the unseeded start.

usage: port/tools/checkrand.py [N]      N draws per seed (default 2000)
prints RANDCHECK OK <seeds> x <N> / RANDCHECK FAIL <seed> <draw> <game> <port>
Mutation that must fail it: 0x3039 -> 0x3038 in wrand.c.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools', 'le'))
from emu import Emu  # noqa: E402

RAND, SRAND, SEED = 0x43088, 0x430ac, 0x64008
# None = no srand (the seed the game starts with); then the values the game seeds with:
# max(racetime, 8) (0x1be4e), max(racetime, v88b7a) (fn_16f24), and the edges
SEEDS = [None, 0, 1, 8, 9, 100, 600, 12345, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff,
         0x80000000, 0xffffffff]

C = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static volatile uint32_t wrand_seed = 1;
#define WRAND_SEED (&wrand_seed)
#include "wrand.c"
int main(int argc, char **argv)
{
    int n = atoi(argv[1]);
    if (argc > 2) srand((unsigned)strtoul(argv[2], NULL, 0));
    for (int i = 0; i < n; i++) printf("%d\n", rand());
    printf("seed %u\n", (unsigned)wrand_seed);
    return 0;
}
'''


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
    work = os.path.join(root, '.work', 'port')
    os.makedirs(work, exist_ok=True)
    src, exe = os.path.join(work, 'checkrand.c'), os.path.join(work, 'checkrand')
    open(src, 'w').write(C)
    subprocess.run(['gcc', '-m32', '-O0', '-fno-builtin', '-I', os.path.join(root, 'port', 'src'),
                    src, '-o', exe], check=True)
    for s in SEEDS:
        e = Emu('ww')
        if s is not None:
            e.call(SRAND, eax=s)
        game = []
        for _ in range(n):
            e.call(RAND)
            game.append(e.reg('eax'))
        args = [exe, str(n)] + ([] if s is None else [str(s)])
        out = subprocess.run(args, capture_output=True, text=True, check=True).stdout.split('\n')
        port = [int(x) for x in out[:n]]
        for i, (g, p) in enumerate(zip(game, port)):
            if g != p:
                print('RANDCHECK FAIL seed %s draw %d game %d port %d' % (s, i, g, p))
                return 1
        if int(out[n].split()[1]) != e.u32(SEED):
            print('RANDCHECK FAIL seed %s: final seed game %#x port %s' % (s, e.u32(SEED), out[n]))
            return 1
    print('RANDCHECK OK %d seeds (and the unseeded start) x %d draws, final seeds equal'
          % (len(SEEDS) - 1, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())

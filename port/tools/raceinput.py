#!/usr/bin/env python3
"""Player input for a race compared frame by frame (port/tools/racecmp.py), given the same way to the
original and to the port: written into the game's own key-state bytes at race_run's `ticks2++`
(0x3114c), keyed on ticks2, so the timing of either side does not matter.

    raceinput.py pokes SCRIPT CODE_DELTA DATA_DELTA SEED > FILE   for tools/dosrun.py watch --pokes
    raceinput.py gdb SCRIPT > FILE                                 gdb commands for the port

SCRIPT: `FRAME CONTROL down|up` per line ('#' comments); FRAME is ticks2 before the increment.
The controls are player one's 12-byte record at 0x7b6d8 (v7d8ac): scancode/state pairs, the state
bytes read by race_run (0x7b6d9 left -> key88cd5, 0x7b6db right -> key88cdd) and buttons_poll
(+5 accelerate joy88b40, +7 fire joy88b42, +9 brake joy88b44, +11 hop joy88b46; findings/input.md).
The pokes file also sets the grid seed (racetime 0x7e71c = SEED at fn_1be3c's entry), as check.sh
does in the port."""
import sys

TABLE = 0x7b6d8
CONTROLS = {'left': 1, 'right': 3, 'accel': 5, 'fire': 7, 'brake': 9, 'hop': 11}
TICK = 0x3114c          # race_run: inc dword [ticks2]
TICKS2 = 0x7ec64
GRID = 0x1be3c          # fn_1be3c: srand(racetime)
RACETIME = 0x7e71c


def load(path):
    ev = []
    for line in open(path):
        f = line.split('#')[0].split()
        if f:
            ev.append((int(f[0]), CONTROLS[f[1]], {'down': 1, 'up': 0}[f[2]]))
    return sorted(ev)


def main(argv):
    ev = load(argv[1])
    if argv[0] == 'pokes':
        cd, dd, seed = int(argv[2], 0), int(argv[3], 0), int(argv[4], 0)
        print('%#x - - %#x %s' % (GRID + cd, RACETIME + dd, seed.to_bytes(4, 'little').hex()))
        for fr, off, v in ev:
            print('%#x %#x %d %#x %02x' % (TICK + cd, TICKS2 + dd, fr, TABLE + off + dd, v))
    elif argv[0] == 'gdb':
        frames = {}
        for fr, off, v in ev:
            frames.setdefault(fr, []).append((off, v))
        for fr in sorted(frames):
            print('if ticks2 == %d' % fr)
            for off, v in frames[fr]:
                print('  set var *(unsigned char *)%#x = %d' % (TABLE + off, v))
            print('end')
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])

#!/usr/bin/env python3
"""test_asmcheck.py: asmcheck.py passes src/wwasm.asm and fails each mutation that should fail it.

  1. one immediate changed (`cmp word [_v88b30], 1` -> 2 in kbd_isr): BIN differs at 0x40919;
  2. one data name replaced by its literal address: same bytes, but the fixup at 0x40915 is gone."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
CHECK = os.path.join(ROOT, 'tools', 'asm', 'asmcheck.py')


def run(path):
    r = subprocess.run([sys.executable, CHECK, path], capture_output=True, text=True)
    return r.returncode, r.stdout


def main():
    if not os.path.exists(os.path.join(ROOT, '.work', 'le', 'ww.obj1.bin')):
        print('skip: no .work/le/ww.* (tools/le/leparse.py original/WW.EXE .work/le/ww)')
        return 0
    src = open(os.path.join(ROOT, 'src', 'wwasm.asm')).read()
    rc, out = run(os.path.join(ROOT, 'src', 'wwasm.asm'))
    assert rc == 0 and out.startswith('OK'), out
    line = 'cmp word [_v88b30], 1'
    assert src.count(line) == 1
    tmp = tempfile.mkdtemp()
    for new, want in ((line[:-1] + '2', 'BIN: 40865 bytes, game 40865; 1 differ, first at 0x40919'),
                      ('cmp word [0x88b30], 1', 'game only 0x40915')):
        p = os.path.join(tmp, 'm.asm')
        open(p, 'w').write(src.replace(line, new))
        rc, out = run(p)
        assert rc == 1 and want in out, (new, out)
    print('test_asmcheck: OK (source passes, 2 mutations fail)')
    return 0


if __name__ == '__main__':
    sys.exit(main())

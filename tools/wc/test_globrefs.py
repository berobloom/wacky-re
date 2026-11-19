#!/usr/bin/env python3
"""test_globrefs.py: the three census traps iter 65 fixed. Run with the venv python."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PY = os.path.join(ROOT, '.work', 'venv', 'bin', 'python')
if not os.path.exists(PY):
    PY = sys.executable


def run(name, addr):
    r = subprocess.run([PY, 'tools/wc/globrefs.py', 'ww', '.work/src/WW.OBJ', name, addr],
                       cwd=ROOT, capture_output=True, text=True)
    assert not r.stderr, r.stderr
    return r.stdout


h = run('fade_out', '0x1bf94').splitlines()[0]      # a prefix of fade_out_screen_
assert '(fade_out_)' in h, h
h = run('main', '0x35ca4').splitlines()[0]          # the last public in _TEXT, a prefix of main_menu_
assert '(main_)' in h, h
out = run('fn_17928', '0x17928')                    # our absolute disp32s are not one-sided
for a in ('0x6e6e0', '0x6e6e4', '0x6508e'):
    assert ('only 0x%x' % int(a, 16)) not in out, (a, out)
print('OK')

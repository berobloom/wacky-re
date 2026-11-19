#!/usr/bin/env python3
"""test_fixsites.py: what the masked-fixup census must and must not say. Run with the venv python."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PY = os.path.join(ROOT, '.work', 'venv', 'bin', 'python')
if not os.path.exists(PY):
    PY = sys.executable

r = subprocess.run([PY, 'tools/wc/fixsites.py', 'ww', '.work/src/WW.OBJ'],
                   cwd=ROOT, capture_output=True, text=True)
assert not r.stderr, r.stderr
out = r.stdout
# the class the census exists for: no MATCHed function reads another global, calls another
# function or passes another literal than the game does at the mirrored site
assert '0 disagree' in out and r.returncode == 0, out
import re
n = int(re.search(r'ext (\d+),', out).group(1))          # the external (global/callee) sites
assert n > 4000, out
# our data segments are offset/data record lists, so their bytes must NOT be compared: the first
# version did and printed 325 phantom "different strings"
m = re.search(r'segment CONST +(\d+) sites, base 0x(60004) \((\d+) agree\)', out)
assert m and int(m.group(1)) > 300 and m.group(1) == m.group(3), out   # all of them, one base
# a table into a near miss is reported, not silently called equal
assert out.count('undecided:') == 6, out

# --arms NAME: the arm blocks themselves. `--arms` is what decides those 54 entries.
r = subprocess.run([PY, 'tools/wc/fixsites.py', 'ww', '.work/src/WW.OBJ', '--arms',
                    'cars_menu', 'kart_fire', 'main_menu', 'trial_book'],
                   cwd=ROOT, capture_output=True, text=True)
assert not r.stderr, r.stderr
arms = r.stdout
m = re.search(r'(\d+)/(\d+) arm blocks equal, (\d+) differ', arms)
assert m and int(m.group(2)) == 36, arms        # 8 + 9 + 13 + 6 entries
# iter 67's bug: the carstat==0 slide arm stores the frame at the arm head (game 0x1b7de). Since iter
# 73's shape scan our arm is byte-identical, so it must stay SAME (a DIFFER here is that bug back).
assert re.search(r'\[0\] game 0x01b7d7 \(cars_menu\+0x1457\)  ours 0x[0-9a-f]+ \(cars_menu\+0x[0-9a-f]+\)  '
                 r'\d+ insns SAME', arms), arms
# ... and the arm below it stores 6 into the same field: the game as an immediate (shows only if 2-byte
# immediates are captured, `mov word [eax+0x6aaa2], 6`), ours from DI, which holds 6 there (iter 74:
# `mov edi, 6` before the table jump, the loop re-enters after it, no edi write or call in the arms)
assert arms.count('73942,imm2:6') == 1, arms          # the game's column
assert re.search(r'ours mov word ptr \[eax \+ #\], di  \[73942\]', arms), arms
# trial_book's six arms are the same bytes at the same address in both builds
assert len(re.findall(r'trial_book\+0x[0-9a-f]+\)  ours 0x02e[45][0-9a-f]+ \(trial_book\+0x[0-9a-f]+\)  '
                      r'\d+ insns SAME', arms)) == 6, arms
print('OK')

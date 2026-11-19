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
# iter 67's bug: the carstat==0 slide arm stores the frame at the arm head (game 0x1b7de), and both
# builds keep the 2 in CX, so the line to pin is the store to 0x73942 in both columns.
arm0 = re.search(r'\n  \[0\] game 0x01b7d7.*\n(.*\n){2}', arms)
assert arm0 and arm0.group(0).count('[73942]') == 2, arms
# ... and the arm below it stores an immediate into the same field, which shows only if 2-byte
# immediates are captured (`mov word [eax+0x6aaa2], 6` keeps its 6 in a 2-byte field)
assert arms.count('73942,imm2:6') == 2, arms          # the game's column and ours
# trial_book's six arms are the same bytes at the same address in both builds
assert len(re.findall(r'trial_book\+0x[0-9a-f]+\)  ours 0x02e[45][0-9a-f]+ \(trial_book\+0x[0-9a-f]+\)  '
                      r'\d+ insns SAME', arms)) == 6, arms
print('OK')

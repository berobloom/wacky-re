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
# iter 67's bug: the carstat==0 slide arm stores the frame at the arm head (game 0x1b7de), so the second
# line of that block must agree on both sides. Iter 75's p7d838 fix (0x1a8f6 once per pass, not once per
# name row) changed which locals cars_menu keeps in registers, so the block no longer prints SAME: its
# only difference is the frame slot the compare reads (game [esp+0x16c], ours [esp+0x144] - the
# documented stack-slot class). Pin the store and the slot pair, not whole-block identity.
b0 = arms.split('\n  [1] game')[0]
assert 'game 0x01b7d7 (cars_menu+0x1457)' in b0, b0
for op in ('game', 'ours'):
    assert ('%s mov bx, word ptr [eax + #]  [7393c]' % op) in b0, b0
    assert ('%s mov word ptr [eax + #], cx  [73942]' % op) in b0, b0
assert ('game cmp bx, word ptr [esp + #]  [16c]' in b0 and
        'ours cmp bx, word ptr [esp + #]  [144]' in b0), b0
# ... and the arm below it stores 6 into the same field: the game as an immediate (shows only if 2-byte
# immediates are captured, `mov word [eax+0x6aaa2], 6`). Iter 74 had ours load it into DI before the
# table jump; since iter 75's cars_menu edit ours stores the immediate too, so it appears twice.
assert arms.count('73942,imm2:6') == 2, arms          # the game's column and ours
# trial_book's six arms are the same bytes at the same address in both builds
assert len(re.findall(r'trial_book\+0x[0-9a-f]+\)  ours 0x02e[45][0-9a-f]+ \(trial_book\+0x[0-9a-f]+\)  '
                      r'\d+ insns SAME', arms)) == 6, arms
print('OK')

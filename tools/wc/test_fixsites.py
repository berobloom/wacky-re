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
print('OK')

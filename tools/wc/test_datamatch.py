#!/usr/bin/env python3
"""test_datamatch.py: what datamatch.py must get right. Run with the venv python.

  .work/venv/bin/python tools/wc/test_datamatch.py

On the committed src/ww.c (compile it first with src/check.sh): the CONST group's base comes out as the
one objmatch.py's fixup census reports, and the 25 differing pool bytes are called dead. Then one byte of
the literal "INITIALIZING MODEM" (0x60004, a dword in the code object holds that address) is flipped in a
copy of the .OBJ: datamatch must call that region ADDRESSED and exit 1.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PY = os.path.join(ROOT, '.work', 'venv', 'bin', 'python')
if not os.path.exists(PY):
    PY = sys.executable
OBJ = os.path.join(ROOT, '.work', 'src', 'WW.OBJ')
MUT = os.path.join(ROOT, '.work', 'datamatch_mut.OBJ')
PAT = b'INITIALIZING MODEM'


def run(obj):
    r = subprocess.run([PY, 'tools/wc/datamatch.py', 'ww', obj], cwd=ROOT,
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


code, out = run(OBJ)
assert code == 0, out
assert 'base 0x060004' in out, out                      # the base of objmatch's fixup census
assert '25 of 9046 compared bytes differ, 25 of them addressed nowhere' in out, out

raw = bytearray(open(OBJ, 'rb').read())
i = raw.find(PAT)
assert i > 0, 'the literal is not in the .OBJ (is .work/src/WW.OBJ current? run src/check.sh)'
raw[i] ^= 0x20                                         # 'I' -> 'i'
open(MUT, 'wb').write(bytes(raw))
code, out = run(MUT)
assert code != 0 and 'ADDRESSED - differs' in out, (code, out.splitlines()[:6])
assert '0x060004' in out, out
print('OK')

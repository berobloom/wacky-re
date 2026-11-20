#!/bin/sh
# Prepare tracing Watcom 9.5b's WCC386 (findings/compiler.md):
#   .work/wcc/wcc386_95b.img  the P3 load image (linear base 0x1000)
#   .work/wcc/ins.pkl         a linear disassembly of it (tools/wcc95/sweep.py)
#   .work/wcc/wcc.conf        DOSBox-X config: C: = the 9.5b install, D: = .work/wcc/probe
# Then: DREA_CONF=$PWD/.work/wcc/wcc.conf tools/dosrun.py start; tools/wcc95/ratrace.py ...
set -eu
cd "$(dirname "$0")/../.."
mkdir -p .work/wcc/probe
.work/venv/bin/python - <<'P'
d = open('.work/toolchains/inst/WC95B/BIN/WCC386.EXE', 'rb').read()
p3 = 0x2c524                                   # end of the MZ stub: the P3 header
assert d[p3:p3 + 2] == b'P3'
img = d[p3 + 0x200:p3 + 0x200 + 0x6ce96]       # load image: file offset 0x200, 0x6ce96 bytes
open('.work/wcc/wcc386_95b.img', 'wb').write(img)
import os
s = open('.work/dos/game.conf').read().replace('cycles=fixed 40000', 'cycles=max')
root = os.getcwd()
s = s[:s.index('[autoexec]')] + '''[autoexec]
mount c "%s/.work/toolchains/inst/WC95B"
mount d "%s/.work/wcc/probe"
set WATCOM=C:\\
set INCLUDE=C:\\H
set PATH=C:\\BIN;C:\\BINB
set DOS4G=QUIET
d:
''' % (root, root)
open('.work/wcc/wcc.conf', 'w').write(s)
P
.work/venv/bin/python tools/wcc95/sweep.py

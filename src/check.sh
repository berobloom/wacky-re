#!/bin/sh
# Compile src/ww.c with the game's options and compare every function with WW.EXE (addresses from
# analysis/ww_names.txt by name), then the fixup targets.   src/check.sh [-v]  (-v: also the diff lines)
# tools/link/extra.c (fn_1c068 / fn_1c0ac, kept out of ww.c so -oe cannot inline them) is compiled
# and compared the same way after it.
set -eu
cd "$(dirname "$0")/.."
w=.work/src; mkdir -p $w; rm -f $w/WW.OBJ $w/EXTRA.OBJ; cp src/ww.c $w/WW.C; cp tools/link/extra.c $w/EXTRA.C
for c in WW EXTRA; do
    tools/wc/wcc.sh .work/toolchains/inst/WC95B $w $c.C -s -3r -oneatx | grep -iE 'error|warning' | grep -v ' 0 warnings, 0 errors' || true
    if [ "${1:-}" = -v ]; then .work/venv/bin/python tools/wc/objmatch.py ww $w/$c.OBJ
    else .work/venv/bin/python tools/wc/objmatch.py ww $w/$c.OBJ | grep -v '^      '; fi
done

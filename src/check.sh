#!/bin/sh
# Compile src/ww.c with the game's options and compare every function with WW.EXE (addresses from
# analysis/ww_names.txt by name), then the fixup targets.   src/check.sh [-v]  (-v: also the diff lines)
set -eu
cd "$(dirname "$0")/.."
w=.work/src; mkdir -p $w; rm -f $w/WW.OBJ; cp src/ww.c $w/WW.C
tools/wc/wcc.sh .work/toolchains/inst/WC95B $w WW.C -s -3r -oneatx | grep -iE 'error|warning' | grep -v ' 0 warnings, 0 errors' || true
if [ "${1:-}" = -v ]; then .work/venv/bin/python tools/wc/objmatch.py ww $w/WW.OBJ
else .work/venv/bin/python tools/wc/objmatch.py ww $w/WW.OBJ | grep -v '^      '; fi

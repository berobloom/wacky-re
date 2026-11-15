#!/bin/sh
# Compile the probe sources with one option set and compare each function with WW.EXE at its address.
#
#   analysis/optpin/check.sh [wcc386 options]      default: -s -3r -oilrt
#
# Needs .work/toolchains/inst/WC95B and .work/le/ww.* (findings/executables.md). Works in .work/optpin.
# Expected with the default: 10 of 11 MATCH; crcstep_ differs (QUESTIONS.md 1).
set -eu
cd "$(dirname "$0")/../.."
[ $# -gt 0 ] || set -- -s -3r -oilrt
opts="$*"
w=.work/optpin; mkdir -p $w; cp analysis/optpin/*.C $w/
while read -r src addrs; do
    # shellcheck disable=SC2086
    tools/wc/wcc.sh .work/toolchains/inst/WC95B $w "$src.C" $opts >/dev/null || { echo "$src.C: compile failed"; exit 1; }
    # shellcheck disable=SC2086
    .work/venv/bin/python tools/wc/objmatch.py ww "$w/$src.OBJ" $addrs
done <<EOF2
T1 0x14218 0x1424c 0x12490 0x248ac
T2 0x102cc 0x10b74 0x1422c 0x14238 0x16a7c 0x341d8
T6 0x109d8
EOF2

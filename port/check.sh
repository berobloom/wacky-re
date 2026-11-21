#!/bin/sh
# port/check.sh: build the port, run it headless and compare its first picture with the original's.
#
#   port/check.sh            -> PORTCHECK OK / PORTCHECK FAIL <shotcmp line>
#
# The title screen ("WACKY WHEELS VERSION 1.1 ... PRESS FIRE", shown about 2 s after start) must be
# pixel-identical to captures/title_ww.png (the original under DOSBox-X, 640x400, captures/MANIFEST.md).
# Mutation that must make it fail: in port/src/vga.c's vga_render, plane[x & 3] -> plane[(x + 1) & 3].
# Runs from a copy of the game directory (.work/port/run, made from original/ on first use).
set -u
cd "$(dirname "$0")/.."
port/build.sh > .work/port/build.log 2>&1 || { echo "PORTCHECK NOBUILD (.work/port/build.log)"; exit 1; }
run=.work/port/run
[ -f $run/WW.EXE ] || { mkdir -p $run && cp original/* $run/; }
shot=$PWD/.work/port/check_title.png
rm -f "$shot"
(cd $run && PORT_HEADLESS=1 PORT_SHOTS=4000:$shot PORT_EXIT=4200 timeout 30 ../wacky > /dev/null 2>&1)
[ -f "$shot" ] || { echo "PORTCHECK FAIL no picture (the port stopped early)"; exit 1; }
r=$(.work/venv/bin/python port/tools/shotcmp.py "$shot" captures/title_ww.png .work/port/check_diff.png)
case "$r" in SAME*) echo "PORTCHECK OK title: $r"; exit 0;; esac
echo "PORTCHECK FAIL title: $r (.work/port/check_diff.png)"; exit 1

#!/bin/sh
# port/check.sh: build the port, run it headless and compare its screens with the original's.
#
#   port/check.sh            -> PORTCHECK OK / PORTCHECK FAIL <which: shotcmp line>
#   port/check.sh --title    only the first check (4 s instead of a minute)
#
# 1. title (QUEUE Q1): the title screen ("WACKY WHEELS VERSION 1.1 ... PRESS FIRE", about 2 s after
#    start) must be pixel-identical to captures/title_ww.png (the original under DOSBox-X, 640x400).
#    Mutation: in port/src/vga.c's vga_render, plane[x & 3] -> plane[(x + 1) & 3].
# 2. menus to the first race (QUEUE Q2): tools/link/racetest.sh's key path (Enter at 8 s and then
#    every 3 s, ten times, 6 s, Enter; 80 ms presses as tools/dosrun.py's) must show the ENGINES menu
#    2.9 s after the 8th Enter and the BRONZE WHEELS overview 8 s after the 11th, each within
#    racetest's tolerance of captures/engines_menu_ww.png and captures/race_overview_ww.png (mean RGB
#    difference under 20, under 3% of pixels over 40). The path depends on the original's timing:
#    the menus take Enter without waiting for its release, and the intro's fades are paced by port
#    I/O (port/README.md "Timing").
#    Mutation: io_cost in port/src/vga.c returns at once (or PORT_IO_NS=0): the fades take 3 ms, the
#    menus change faster than a keypress, and the 8th Enter's screen is the zone select (engines
#    mean 109.45, 97.50%).
# Runs from a copy of the game directory (.work/port/run, made from original/ on first use).
set -u
cd "$(dirname "$0")/.."
port/build.sh > .work/port/build.log 2>&1 || { echo "PORTCHECK NOBUILD (.work/port/build.log)"; exit 1; }
run=.work/port/run
[ -f $run/WW.EXE ] || { mkdir -p $run && cp original/* $run/; }
out=$PWD/.work/port
cmp() { .work/venv/bin/python port/tools/shotcmp.py "$1" "$2" "$3"; }

rm -f "$out/check_title.png"
(cd $run && PORT_HEADLESS=1 PORT_SHOTS=4000:$out/check_title.png PORT_EXIT=4200 timeout 30 ../wacky > /dev/null 2>&1)
[ -f "$out/check_title.png" ] || { echo "PORTCHECK FAIL title: no picture (the port stopped early)"; exit 1; }
r=$(cmp "$out/check_title.png" captures/title_ww.png "$out/check_title_diff.png")
case "$r" in SAME*) ;; *) echo "PORTCHECK FAIL title: $r ($out/check_title_diff.png)"; exit 1;; esac
[ "${1:-}" = --title ] && { echo "PORTCHECK OK title: $r"; exit 0; }

keys=""; t=8000
for i in 1 2 3 4 5 6 7 8 9 10; do
    keys="$keys$t:1c,$((t + 80)):9c,"
    [ $i = 8 ] && engines_at=$((t + 2900))
    t=$((t + 3000))
done
t=$((t + 6000))
keys="$keys$t:1c,$((t + 80)):9c"
rm -f "$out/check_engines.png" "$out/check_overview.png"
(cd $run && PORT_HEADLESS=1 PORT_KEYS=$keys \
    PORT_SHOTS=$engines_at:$out/check_engines.png,$((t + 8000)):$out/check_overview.png \
    PORT_EXIT=$((t + 8300)) timeout 120 ../wacky > /dev/null 2>&1)
for pair in engines:engines_menu_ww overview:race_overview_ww; do
    name=${pair%%:*}; ref=captures/${pair#*:}.png; shot=$out/check_$name.png
    [ -f "$shot" ] || { echo "PORTCHECK FAIL $name: no picture (the port stopped early)"; exit 1; }
    r=$(cmp "$shot" "$ref" "$out/check_${name}_diff.png")
    # racetest's tolerance: mean < 20 and under 3% of pixels over 40
    echo "$r" | awk '{ p = $7; gsub(/[(%)]/, "", p); exit !($3 < 20 && p < 3) }' ||
        { echo "PORTCHECK FAIL $name: $r ($out/check_${name}_diff.png)"; exit 1; }
    eval "r_$name=\"\$r\""
done
echo "PORTCHECK OK title SAME; engines: $r_engines; overview: $r_overview"

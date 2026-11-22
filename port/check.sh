#!/bin/sh
# port/check.sh: build the port, run it headless and compare its screens and sound with the original's.
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
# 3. sound: Enter on the title at 8 s, the port's sound recorded (PORT_WAV) to 17 s, must play the
#    Apogee song and the logo's voice as captures/apogee_ww.wav does (the original under DOSBox-X):
#    port/tools/audiocmp.py SAME, notes and timing per 20 ms in semitone bands, mean correlation at
#    least 0.9 (0.956). Mutations: audio.c writes the OPL data to register reg_index + 1 (0.848);
#    sb_read takes the 16-bit ring as signed (0.838).
# 4. the race: the check-2 key path plus Space at the overview, the grid seed forced to 459 (the
#    original's run; racetime at fn_1be3c), gdb dumping the eight player records at every
#    `ticks2++` of race_run; port/tools/racecmp.py against captures/race_trace_ww.bin (the original
#    under DOSBox-X, tools/dosrun.py watch at 0x3114c): 600 frames, every byte equal but the heap
#    pointer f5e (one constant delta). The race steps per frame, so gdb's slowness does not matter.
#    Mutation: see port/README.md "Checks".
# 5. the race with player input: as 4, with captures/race_inputs.txt written into player one's key
#    state bytes at each frame (port/tools/raceinput.py), 800 frames against captures/race_drive_ww.bin,
#    with karts[] (0x68410) in the records: their pointer fields may differ by one constant each.
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

wav=$out/check_apogee.wav
rm -f "$wav"
(cd $run && PORT_HEADLESS=1 PORT_WAV=$wav PORT_KEYS=8000:1c,8080:9c PORT_EXIT=17000 timeout 40 ../wacky > /dev/null 2>&1)
[ -f "$wav" ] || { echo "PORTCHECK FAIL music: no recording (the port stopped early)"; exit 1; }
r_music=$(python3 port/tools/audiocmp.py "$wav" captures/apogee_ww.wav) ||
    { echo "PORTCHECK FAIL music: $r_music"; exit 1; }
line=$(awk '/^void race_run\(void\)$/ {f = 1} f && /^    ticks2\+\+;$/ {print NR; exit}' src/ww.c)
# race_trace NAME FRAMES [INPUT_SCRIPT [karts]]: the race traced at every ticks2++ into
# $out/check_NAME.bin; with karts the records also hold karts[] (an RCK1 file, port/tools/racecmp.py)
race_trace() {
trace=$out/check_$1.bin
rm -f "$trace"
inputs=""
[ -n "${3:-}" ] && inputs=$(python3 port/tools/raceinput.py gdb "$3")
kartdump=""
[ "${4:-}" = karts ] && { printf RCK1 > "$trace"; kartdump="append binary memory $trace 0x68410 0x68c30"; }
cat > $out/check_$1.gdb <<EOF
set pagination off
set confirm off
break fn_1be3c
commands
silent
set var racetime = 459
cont
end
break src/ww.c:$line
commands
silent
append binary memory $trace 0x7ec64 0x7ec68
append binary memory $trace 0x6aa90 0x6add0
append binary memory $trace 0x70720 0x70730
$kartdump
$inputs
if ticks2 >= $2
  kill
end
cont
end
run
EOF
(cd $run && PORT_HEADLESS=1 PORT_KEYS=$keys,$((t + 8500)):39,$((t + 8580)):b9 PORT_EXIT=$((t + 200000)) \
    timeout 400 gdb -q -batch -x $out/check_$1.gdb --args ../wacky > /dev/null 2>&1 < /dev/null)
cp original/WACKY*.HI original/WACKY*.TIM original/WACKY.CFG $run/ 2>/dev/null
[ -f "$trace" ] || { echo "PORTCHECK FAIL $1: no trace (the port stopped early)"; exit 1; }
}
race_trace race 600
r_race=$(python3 port/tools/racecmp.py captures/race_trace_ww.bin "$trace" 600) ||
    { echo "PORTCHECK FAIL race: $r_race"; exit 1; }
race_trace drive 800 captures/race_inputs.txt karts
r_drive=$(python3 port/tools/racecmp.py captures/race_drive_ww.bin "$trace" 800) ||
    { echo "PORTCHECK FAIL drive: $r_drive"; exit 1; }
echo "PORTCHECK OK title SAME; engines: $r_engines; overview: $r_overview; music: $r_music; race: $r_race; drive: $r_drive"

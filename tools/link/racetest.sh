#!/bin/sh
# racetest.sh: build the hybrid WW.EXE (tools/link/build.sh) and drive it in the harness from the
# DOS prompt to the first race's track overview (Enter x 11: title, intro, SINGLE PLAYER, vehicle,
# class, laps, engines, regular races, zone select, LOADING RACE 1), then compare the screen with
# the original's (captures/race_overview_ww.png). Prints exactly one line:
#   RACETEST OK <diff>      the screen is the original's (mean RGB difference < 20)
#   RACETEST FAIL <diff>    it is not (screenshot in .work/link/racetest.png)
#   RACETEST NOBUILD        the hybrid did not link (see tools/link/build.sh's output)
#   RACETEST NOHARNESS      DOSBox-X did not come up twice
# ~4 minutes. Uses the harness: never run it while anything else uses it (one client per port).
#   tools/link/racetest.sh                    our C as it is
#   ORIG=fn_a,fn_b tools/link/racetest.sh     with those C functions from the game's bytes
#   tools/link/racetest.sh --nobuild EXE      run a given EXE (the control: .work/link/WWORIG.EXE)
set -u
cd "$(dirname "$0")/../.."
if [ "${1:-}" = "--nobuild" ]; then
    exe=$2
else
    tools/link/build.sh > .work/link/racetest.build 2>&1
    exe=.work/link/WWNEW.EXE
    grep -q 'WWNEW.EXE' .work/link/racetest.build && [ -f $exe ] || { echo "RACETEST NOBUILD"; exit 1; }
fi
cp "$exe" .work/dos/C/WACKY/WWT.EXE
up=0
for try in 1 2; do
    overnight/cleanup.sh >/dev/null 2>&1; sleep 3
    python3 tools/dosrun.py start >/dev/null 2>&1; sleep 2
    if python3 tools/dosrun.py type 'cd \WACKY' >/dev/null 2>&1; then up=1; break; fi
done
[ $up = 1 ] || { overnight/cleanup.sh >/dev/null 2>&1; echo "RACETEST NOHARNESS"; exit 1; }
python3 tools/dosrun.py type 'WWT' >/dev/null 2>&1; sleep 8
for i in 1 2 3 4 5 6 7 8 9 10; do python3 tools/dosrun.py key ret >/dev/null 2>&1; sleep 3; done
sleep 6
python3 tools/dosrun.py key ret >/dev/null 2>&1; sleep 8
rm -f .work/link/racetest.png
python3 tools/dosrun.py shot .work/link/racetest.png >/dev/null 2>&1
overnight/cleanup.sh >/dev/null 2>&1
[ -f .work/link/racetest.png ] || { echo "RACETEST NOHARNESS"; exit 1; }
python3 -c "
from PIL import Image, ImageChops, ImageStat
a = Image.open('.work/link/racetest.png').convert('RGB')
b = Image.open('captures/race_overview_ww.png').convert('RGB')
d = sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3
print('RACETEST %s %.1f' % ('OK' if d < 20 else 'FAIL', d))"

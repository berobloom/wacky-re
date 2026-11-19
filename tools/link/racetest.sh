#!/bin/sh
# racetest.sh: build the hybrid WW.EXE (tools/link/build.sh) and drive it in the harness from the
# DOS prompt to the first race's track overview (Enter x 11: title, intro, SINGLE PLAYER, vehicle,
# class, laps, engines, regular races, zone select, LOADING RACE 1), then compare the screen with
# the original's (captures/race_overview_ww.png). Before the 9th Enter the screen is the ENGINES
# menu (engines_menu 0x167d8 -> menu 0x155c0 -> msgbox), so every menu screen is screenshotted and
# menu8 is compared with captures/engines_menu_ww.png - the menu banner-y bug (0x15fad) is invisible
# in the overview. Prints exactly one line, worst screen wins:
#   RACETEST OK <metrics>   every screen is the original's (mean RGB difference < 20 and under 3%
#                           of its pixels differing by more than 40). Noise floor measured over two
#                           runs of the ORIGINAL: engines 0.45%, race grid 1.2-2.1% (the karts bob on
#                           the start line), overview 0.00%. menu 0x155c0's wrong banner y reads 10.8%
#                           Plus per-widget colour counts (regions[] below), shot/reference,
#                           which must be within 5%: `place 1112/1112`
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
for i in 1 2 3 4 5 6 7 8 9 10; do
    python3 tools/dosrun.py key ret >/dev/null 2>&1; sleep 3
    rm -f .work/link/racetest_menu$i.png
    python3 tools/dosrun.py shot .work/link/racetest_menu$i.png >/dev/null 2>&1
done
sleep 6
python3 tools/dosrun.py key ret >/dev/null 2>&1; sleep 8
rm -f .work/link/racetest.png
python3 tools/dosrun.py shot .work/link/racetest.png >/dev/null 2>&1
# the overview's own dismiss key starts the race; the shot 3 s later is inside it (the first frames
# are where cpu_drive 0x28cac reads players[].f14, iter 69's fix). Compared only if a reference of
# the same instant exists - a moving race is not reproducible by wall-clock key holds (GOTCHAS).
python3 tools/dosrun.py key spc >/dev/null 2>&1; sleep ${RACEDELAY:-3}
rm -f .work/link/racetest_race.png
python3 tools/dosrun.py shot .work/link/racetest_race.png >/dev/null 2>&1
overnight/cleanup.sh >/dev/null 2>&1
[ -f .work/link/racetest.png ] || { echo "RACETEST NOHARNESS"; exit 1; }
python3 -c "
import os, sys
from PIL import Image, ImageChops, ImageStat
def diffs(a, b):
    d = ImageChops.difference(Image.open(a).convert('RGB'), Image.open(b).convert('RGB'))
    px = list(d.getdata())
    # mean RGB alone hid menu 0x155c0's wrong banner y (9.5, under the 20): a small block moved on a
    # 640x400 screen barely moves the mean, so the fraction of clearly-different pixels decides too
    return (sum(ImageStat.Stat(d).mean) / 3,
            sum(1 for p in px if max(p) > 40) * 100.0 / len(px))
pairs = [('.work/link/racetest.png', 'captures/race_overview_ww.png', 'overview'),
         ('.work/link/racetest_menu8.png', 'captures/engines_menu_ww.png', 'engines'),
         ('.work/link/racetest_race.png', 'captures/race_play_ww.png', 'race'),
         # the ORIGINAL draws one of two start grids (opponents and order), by timing: 1 run in 4 of
         # WWORIG.EXE drew grid B. Each grid has its own reference and the closer one counts.
         ('.work/link/racetest_race.png', 'captures/race_play_ww_b.png', 'race')]
best = {}
for shot, base, name in pairs:
    if not os.path.exists(base):
        continue                                   # no reference for this screen yet
    if not os.path.exists(shot):
        print('RACETEST FAIL %s mean 999.0 bad 100.0 (no screenshot)' % name); sys.exit(0)
    d = diffs(shot, base)
    if name not in best or d[1] < best[name][1]:
        best[name] = d
out = [(n,) + best[n] for n in ('overview', 'engines', 'race') if n in best]
ok = all(m < 20 and bp < 3.0 for _, m, bp in out)
# Whole-screen numbers hide a small wrong widget: the race place icon drawn from a NULL table
# (QUESTIONS 25) moved the race screen only to mean 2.3 / 2.22% bad, under both limits. So count one
# exact colour inside a known widget, shot against reference, and fail past 5% apart.
# (name, shot, reference, crop box in shot px, RGB)
regions = [('place', '.work/link/racetest_race.png', 'captures/race_play_ww.png',
            (50, 24, 94, 88), (255, 255, 0))]     # the 22x29 LAP.SP place icon at game px 25,12
reg = []
for name, shot, base, box, rgb in regions:
    if not (os.path.exists(base) and os.path.exists(shot)):
        continue
    n = [sum(1 for p in Image.open(f).convert('RGB').crop(box).getdata() if p == rgb)
         for f in (shot, base)]
    reg.append('%s %d/%d' % (name, n[0], n[1]))
    ok = ok and abs(n[0] - n[1]) <= 0.05 * max(n[1], 20)
print('RACETEST %s %s' % ('OK' if ok else 'FAIL',
                          ' '.join(['%s mean %.1f bad %.2f%%' % t for t in out] + reg)))"

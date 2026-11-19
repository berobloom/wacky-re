# captures/ MANIFEST

**Do not move, rename or delete a file named here: checks and tools read these
paths.** Anything not named here is one-shot evidence and may be purged;
regenerate it with the same call if needed.

Dumps and save states hold game code, so `.gitignore` keeps them local:
this file records how to regenerate them.

Record for each: what it is, how and when it was made (boot, segments or LE
deltas of that boot, the scene), and what reads it.

## Save states

| file | scene | boot (segments / deltas) | used by |
|---|---|---|---|
| `.work/dos/states/bonus22.sav` | BONUS BRONZE WHEELS overview of track 22, before the race: made from `trackpick.sav` (boot `WW`, then load) by poking `v7ecb4` (lin 0x257cb4) = 3 and pressing Enter (iter 56c) | LE code 0x10000 -> 0x1ec000 (delta 0x1dc000), data 0x60000 -> 0x239000 (delta 0x1d9000) | QUESTIONS 19/20: `track` = 22, the `.PAR` plane dump at A000:7d00 and the window check |
| `captures/race_overview_ww.png` (local only: game screen, git-ignored; regenerate with `tools/dosrun.py shot` after the run below) | BRONZE WHEELS overview of track 1 (640x400 QMP shot): `WW` - or `.work/link/WWORIG.EXE`, byte-identical objects, diff 0.0 - from the DOS prompt, Enter x 10, 6 s, Enter, 8 s (2026-11-19) | fresh boot, no state | `tools/link/racetest.sh` reference |
| `captures/engines_menu_ww.png` (local only: game screen, git-ignored; regenerate with `tools/link/racetest.sh --nobuild .work/link/WWORIG.EXE`, then `cp .work/link/racetest_menu8.png captures/engines_menu_ww.png`) | the ENGINES menu with its 'LEE & JACKSON LAWNMOWERS' sponsor banner (iter 71: the 8th Enter's screen, not the 6th - the shot after Enter 6 is the class menu), same keyboard path as the row above (2026-11-19) | fresh boot, no state | `tools/link/racetest.sh` reference; catches a wrong msgbox banner y (`menu` 0x15fad): our fixed C 0.7 mean / 0.73% bad pixels, the pre-fix 0x3c literal 9.5 / 10.82% |
| `captures/race_play_ww_b.png` (local only: game screen, git-ignored; a `--nobuild .work/link/WWORIG.EXE` run whose race shot drew grid B, copied from `.work/link/racetest_race.png`) | the same pre-race instant as `race_play_ww.png` with the original's other start grid (findings/game-loop.md `## Traps`, 1 run in 4) (2026-11-19) | fresh boot, no state | `tools/link/racetest.sh` second race reference; the closer of the two counts. Against it our B run reads 0.00% bad, against A 2.55% |
| `captures/race_play_ww.png` (local only: game screen, git-ignored; regenerate with `RACEDELAY=3 tools/link/racetest.sh --nobuild .work/link/WWORIG.EXE`, then `cp .work/link/racetest_race.png captures/race_play_ww.png`) | the pre-race grid 3 s after the overview's dismiss key (`spc`): HUD box top-left, six karts on the start line, 'TIGI', 0 MPH, 0:00:0 (640x400 shot of the 320x200 screen, so 2x sprite coordinates), same keyboard path (2026-11-19) | fresh boot, no state | `tools/link/racetest.sh` reference: our C 2.22% bad pixels, two runs of the original 1.2-2.1% apart (the karts bob); the excess is the box figure of QUESTIONS 25 (findings/video.md) |

## Dumps

| file | linear range / segment:offset | taken when | used by |
|---|---|---|---|

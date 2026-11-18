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

## Dumps

| file | linear range / segment:offset | taken when | used by |
|---|---|---|---|

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

## Dumps

| file | linear range / segment:offset | taken when | used by |
|---|---|---|---|

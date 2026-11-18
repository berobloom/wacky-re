# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **201 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`). C covers
  0x10260-0x36a4d bar the gap 0x1bf7e-0x1c0f3 (a fade-out); 0x36a4e on is the
  hand-written asm module.
- Near misses: `src/nearmiss.txt` is the work list (ranked by `tools/wc/nearmiss.sh`; status
  column MATCH / SKIP: cause / LAYOUT / empty = open). Each function's causes and tried spellings
  are in its `differs:` note in `src/ww.c`.
- What works and what does not: GOTCHAS.md "This project's traps" - *Reading a near-miss diff*,
  *Levers that produced MATCHes*, *Dead ends*. Do not copy levers into this file.
- Findings, evidence at the links: serial (findings/serial.md) rings 0x84b00/0x80afa, wrap 0x4000;
  classes (findings/classes.md) `v88bb2` is 1 AMATEUR..5 KID MODE **signed**; car select
  (findings/car_select.md) `ents[8]` 0x73998 is the box; scores (findings/scores.md) `tbl77758`;
  disc files (findings/formats.md) WACKY.DAT = 577 records of 22 bytes, offsets from 2, 42 sets of
  per-track files.

## Next

1. Near misses: iter 56's lever is `tools/wc/permute.py` (MATCHING.md section 7): release_check
   MATCH, gains in 25 near misses; every winner verified in place and its diff read. No open status in `src/nearmiss.txt`; iter 55 re-probed `players_project` 0x255d4
   (10 loop-shape variants, all = control: the `v7d800` register cache is not a source-shape fact).
   Take one only with an idea its `differs:` note does not list. nmrefs after every change;
   `src/check.sh` count +1 before `src/matched.baseline` goes up.
2. Live harness, next time it is up (QUESTIONS 19 second half): pick the BONUS PACK ZONES choice
   (`zones_menu` 0x16664 -> `v7ecb4` = 3), confirm the loaded track is 16..20, and check whether
   `in_zone` 0x125bc / the `map_at` users do anything at all in such a race.
3. The C gap: the fade-out 0x1bf7e-0x1c0f3 with its out-of-line tail at 0x32cc1-0x32cf7
   (`pic_show`'s padding diff). (0x1f824-0x1f857 was main_menu's jump table, iter 56c.)
4. `.PAR` pixel geometry (QUESTIONS 20): needs one harness memory read of 0xa7d00..0xa95ff after
   the blit - do it with the next live-harness session, do not guess it in the port.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

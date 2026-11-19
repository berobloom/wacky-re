# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **202 of 283 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`). C covers
  all of 0x10260-0x36a4d (no gaps left, iter 56c); 0x36a4e on is the
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

1. Behaviour night is done for the address/const/count class: every one of the 21 nmrefs candidates
   carries `BEHAV:`, 0 open bugs. Iter 64 closed the last three, all counts-only - draw_status
   0x12f80, lights_project 0x266a8, race_results 0x2bff0 - all SAME (a shared `imul`, an in-place
   `add [g],imm` vs load+store, a flag test Watcom folds: GOTCHAS). Next in that class: run globrefs
   over the 0-row LAYOUT near misses (demo_rec 0x2e318, pic_show 0x32c98, fade_out 0x1bf94) - their
   bytes differ only in jump displacements, so the per-address counts are the check fndiff cannot do.
   Ignore `game only <string>` lines (our literals are segment fixups it skips); the rest of the line
   is a question, not a finding.
2. Hybrid link (`tools/link/racetest.sh`): last run RACETEST OK (the first race's overview, iter 60);
   iters 61-64 changed no executable code, so no racetest was run. Re-run it once next iteration and
   then drive into the CHOOSE VEHICLE screen and compare it - iter 60's two cars_menu fixes (the
   centred caption, the selection box drawn a third time) show there and nowhere near the overview.
3. Race frames need the same screen-level comparison (the 0x7b238 animation table, a `clipy` sprite
   clipped at row 199, findings/video.md).
4. QUEUE.md still holds only the unfilled template item (`Q1 <short title>`, empty Report/Accept), so
   iters 61-64 took the overnight prompt's step 2 again; the human should put a real item there.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

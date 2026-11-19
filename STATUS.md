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

1. Behaviour night (overnight/prompt.md step 2): iter 63 took the last three near misses that
   nmrefs flagged by address or const - shot_step 0x25e6c, ents_show 0x014380, fn_1be3c 0x01be3c -
   0 bugs; their one-sided blocks were Watcom shapes (a tail-merged store pair 0x26021/0x26008, a
   block the game proves unreachable 0x145c3 -> 0x146bd, one head for nested retry loops 0x1beae);
   GOTCHAS has the three-class rule. Every nmrefs DIFFER/consts candidate now carries `BEHAV:`.
   What is left of that list: the counts-only trio draw_status 0x12f80, lights_project 0x266a8,
   race_results 0x2bff0 (each `ours more 1x joy88b40`) - do the store census first (GOTCHAS).
2. Run globrefs (`tools/wc/globrefs.py ww .work/src/WW.OBJ NAME 0xADDR`) over the whole near-miss
   list in one loop and triage the hits - it finds this bug class in MATCHed functions too, where
   fndiff has 0 rows. Ignore its `game only <string>` lines (our own literals are segment fixups it
   skips, GOTCHAS); expect address-shape noise otherwise too: the line is a question, not a finding.
3. Hybrid link (`tools/link/racetest.sh`): still RACETEST OK (the first race's overview) - iters 61-
   63 changed no executable code (comment-only), so no racetest was run since iter 60. Next: drive
   into the CHOOSE VEHICLE screen and compare it - iter 60's two cars_menu fixes (the centred
   caption, the selection box drawn a third time) show there and nowhere near the overview; the race
   frames need the same (the 0x7b238 animation table, a `clipy` sprite clipped at row 199).
4. QUEUE.md still holds only the unfilled template item (`Q1 <short title>`, empty Report/Accept),
   so iters 61-63 took the overnight prompt's step 2 again; the human should put a real item there.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

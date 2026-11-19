# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **202 of 283 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`), and iter
  66 checked what a MATCH masks inside those 202: all 4556 linker-filled operands point where the
  game's point (`tools/wc/fixsites.py`, 0 disagreements; `test_fixsites.py`). C covers
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

1. Screen-level is the only lever left. `tools/link/racetest.sh` ran RACETEST OK in iter 67 (the last
   code change: `cars_menu`'s carstat==0 slide arm now stores `ents[i].frame = 2`, game 0x1b7de), so the
   hybrid build is good - run it again at the next code change, then drive into CHOOSE VEHICLE and
   compare the slide (the icon frame each row gets while sliding is what that store decides,
   findings/car_select.md), then the race frames (the 0x7b238 animation table, a `clipy` sprite clipped
   at row 199, findings/video.md).
2. The switch-table census class is closed: all 54 table entries in the 6 tables that sit inside MATCHed
   functions are decided (QUESTIONS 22), one of them a real BUG now fixed. `tools/wc/fixsites.py
   --arms NAME [-v]` prints the two arm blocks side by side if a table is ever added; 16 pairs still
   print DIFFER because a register or a stack slot differs - each one's verdict is in the function's
   `BEHAV:` cell in src/nearmiss.txt, so re-reading them is wasted time.
3. `tools/wc/nearmiss.sh` (~5 min) is overdue and its first column drifts both ways (iter 67 corrected
   cars_menu 433 -> 431 by hand; fn_17928 read 7 in the file and 25 on the committed code, iter 65).
4. QUEUE.md still holds only the unfilled template item (`Q1 <short title>`, empty Report/Accept), so
   iters 61-67 took the overnight prompt's step 2 again; the human should put a real item there.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

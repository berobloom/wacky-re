# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **207 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; iter 74: check.sh also
  compiles `tools/link/extra.c`, whose `fn_1c068`/`fn_1c0ac` were always MATCH but never counted; objmatch
  compares rel32 branches between our own functions by target, which made demo_rec/pic_show/fade_out
  MATCH, `tools/wc/test_objmatch.py`), and iter
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

1. `cars_menu` 0x1a380 returns under `TICK=8 SEED=0x88cce=1:b` and prints **DIFF 1 byte**
   (findings/menus.md): adjudicate it - difffn re-runs with a watch and prints the instruction that
   writes the byte on each side. A DIFF you cannot place in our C, game instruction in front of you,
   is `DIFF unknown`, not "fixed". Then `menu` 0x155c0, `engines_menu` 0x167d8 and `race_results`
   0x2bff0: same two seeds already get both sides home and print SAME, each still needs its control
   run and its `SEEDED:` line.
2. `intro` 0x348f4, `credits_screen` 0x1efb0, `race_records_menu` 0x1d13c, `main_menu` 0x1f858 and
   `fn_1832c` 0x1832c moved from the 3M-instruction cap to the 300-call cap with those seeds: try
   `CALLS=1200` and see whether both sides return.
3. `com_open` 0x102f4, `showlines` 0x2a268, `volume_menu` 0x1e760, `chat` 0x14d00 still sit at the
   instruction cap with the seeds: `FN=<name> .work/venv/bin/python tools/wc/hotfn.py` prints the loop
   they burn in, seed what it polls. `kart_draw` 0x24bcc and `fn_193a8` 0x193a8 crash at the same
   instruction on both sides (a null pointer global) - seed the pointer, do not call it a pass.
4. After every `tools/link/build.sh`: `tools/link/fixcensus.py` must print `0 unexplained` (GOTCHAS,
   first trap). Iter 75 re-ran it over the iter-74 build: 0 unexplained.
5. Register homes (QUESTIONS 2): most of the 78 near misses differ only in which register or stack
   slot 9.5b picks; permute.py runs of 15 min gave 0-1 rows (iter 74). Not an overnight item.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

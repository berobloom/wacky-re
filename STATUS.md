# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **207 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; iter 83 still 207 -
  no C change, `difffn.py`/`hotfn.py` work only; iter 75 re-ran check.sh after the cars_menu behaviour fix: still 207; iter 74: check.sh also
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

1. `demo_mode` 0x1e2e4 (`still capped`): its hot loop is `fade_out` 0x1bf94's palette scan
   (0x1c03a / 0x32cc5-0x32cd7), which converges only once the fade step `v88c46` is non-zero - add
   `SEED=0x88c46=4:w` to its existing env (`RAND=8 FREAD=13 SEED=0x88cd0=1:b,0x7ec58=5:d`), plus
   `0x7d998=2:d` for the race abort arm (findings/game-loop.md).
2. `fade_in` 0x32cf8 reads the fade step `v88c46` at 0x32d78/0x32d95 and a direct call leaves it 0 (the
   game stores 4 at 0x36334) - `difffn.py fade_in FREAD=13 SEED=0x88c46=4:w` (untested).
3. The three menu lines that say `still capped` because one static seed cannot press and release:
   `volume_menu` 0x1e760, `main_menu` 0x1f858, `race_records_menu` 0x1d13c - rerun each with
   `SEED=0x88cd0=1:b,RESEED=0x88cd0=0:b` (the pair that returned `fn_1d704` and `trial_book`) and a
   raised `CALLS=`; verdicts into `src/nearmiss.txt`.
4. `intro` 0x348f4 (`still capped`): its waits read `ticks` with nothing callable in the body - try
   `TICKREAD=8` with that same press/release pair; it also stores `v7d998` itself (0x34b0e, 0x34e5c).
5. Wider walks of the two functions closed this iter: `trial_book` with `TICKREAD=8` to get past
   0x2f193 into the record block and its three call-free `ticks` waits, and `race_run` with
   `SEED=0x7d998=2:d` alone (no `0x88cd0`) to walk past the first frame.
6. After every `tools/link/build.sh`: `tools/link/fixcensus.py` must print `0 unexplained` (GOTCHAS,
   first trap). After a C change, also re-run the behaviour sweep (`awk '$2 ~ /^0x/ && $4 !~ /^MATCH/
   {print $3}' src/nearmiss.txt | while read n; do .work/venv/bin/python tools/wc/difffn.py $n; done`)
   and compare the DIFFs against the recorded verdicts in `src/nearmiss.txt`; the known uncapped DIFFs
   are the dead `v7d7f4`/`v7d874` pointers (findings/game-loop.md).
7. Register homes (QUESTIONS 2): most of the 78 near misses differ only in which register or stack
   slot 9.5b picks; permute.py runs of 15 min gave 0-1 rows (iter 74). Not an overnight item.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

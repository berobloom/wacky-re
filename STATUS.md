# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **207 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; iter 80 still 207 -
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

1. `demo_mode` 0x1e2e4, `fn_1be3c` 0x1be3c (CALLCAP): iter 80's recipe `FREAD=13
   SEED=0x88cd0=1:b,0x7ebc0=1:d,0x88c46=4:w CALLS=200000 COUNT=20000000` made `fn_17928` return with
   both sides agreeing; `hotfn.py` takes the same knobs and says which loop a run sits in.
2. `fade_in` 0x32cf8 reads the fade step `v88c46` at 0x32d78/0x32d95, and a direct call leaves it 0
   (the game stores 4 at 0x36334) - `difffn.py fade_in FREAD=13 SEED=0x88c46=4:w` may return.
3. The press-and-release class - `main_menu` 0x1f858, `race_records_menu` 0x1d13c, `volume_menu`
   0x1e760 - needs `RESEED=` (seeds re-written at every stubbed call/interrupt, GOTCHAS): a static
   seed can press but never release, so `wait_release` 0x278c4 spins (iter 78/80).
4. `menu` 0x155c0 with `ARGS=` (iter 76's register arguments): zero arguments leave after 604 insns,
   so its frame wait 0x16004/0x1604c and its input arm are still untested (findings/menus.md).
5. After every `tools/link/build.sh`: `tools/link/fixcensus.py` must print `0 unexplained` (GOTCHAS,
   first trap). After a C change, also re-run the behaviour sweep (`awk '$2 ~ /^0x/ && $4 !~ /^MATCH/
   {print $3}' src/nearmiss.txt | while read n; do .work/venv/bin/python tools/wc/difffn.py $n; done`)
   and compare the DIFFs against the recorded verdicts in `src/nearmiss.txt`; the known uncapped DIFFs
   are the dead `v7d7f4`/`v7d874` pointers (findings/game-loop.md).
5. After every `tools/link/build.sh`: `tools/link/fixcensus.py` must print `0 unexplained` (GOTCHAS,
   first trap). After a C change, also re-run the behaviour sweep (`awk '$2 ~ /^0x/ && $4 !~ /^MATCH/
   {print $3}' src/nearmiss.txt | while read n; do .work/venv/bin/python tools/wc/difffn.py $n; done`)
   and compare the DIFFs against the recorded verdicts in `src/nearmiss.txt`; the known uncapped DIFFs
   are the dead `v7d7f4`/`v7d874` pointers (findings/game-loop.md).
6. Register homes (QUESTIONS 2): most of the 78 near misses differ only in which register or stack
   slot 9.5b picks; permute.py runs of 15 min gave 0-1 rows (iter 74). Not an overnight item.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

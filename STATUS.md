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

1. Overnight (prompt step 2): the 28 near misses whose `difffn.py` run stops at the 3M-instruction or
   300-call cap. Seed the global each wait loop polls (`SEED=0xADDR=VALUE[:b|:w|:d]`, iter 74; the
   control `NEW=wworig MAP=.work/link/WWORIG.MAP` must stay SAME), get both sides to return, then
   SAME / BUG / DIFF same-shape, recorded as `SEEDED:` in `src/nearmiss.txt`.
2. After every `tools/link/build.sh`: `tools/link/fixcensus.py` must print `0 unexplained` (GOTCHAS,
   first trap). Iter 74 checked iter 73's repaired sites at runtime in both builds (v7d884, v7d888,
   v7d87c, v7eb84 relocated the same; findings/game-loop.md has the start-grid trap).
3. Re-run the behaviour sweep after every C change (78 lines, about 90 s, one process per function):
   `awk '$2 ~ /^0x/ && $4 !~ /^MATCH/ {print $3}' src/nearmiss.txt | while read n; do
   .work/venv/bin/python tools/wc/difffn.py $n; done`. Every uncapped DIFF is adjudicated
   (`ents_project`/`shots_project` = the dead `v7d7f4`/`v7d874` pointers, findings/game-loop.md), so
   a new DIFF is news.
4. `com_wait_byte` 0x2df70 has no call site in the C module (0x10260-0x36a4d), so its EAX
   (0xffffffff/0x0) is unadjudicated: its caller is in the asm module above 0x36a4d, which difffn stubs.
   A whole-module dump has to be chunked - `tools/le/disasm.py` stops at the first undecodable byte.
5. Register homes (QUESTIONS 2): most of the 78 near misses differ only in which register or stack
   slot 9.5b picks; permute.py runs of 15 min gave 0-1 rows (iter 74). Not an overnight item.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

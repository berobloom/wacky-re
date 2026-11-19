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

1. Run `tools/link/fixcensus.py` after every `tools/link/build.sh` (must print `0 unexplained`):
   iter 73 found the race place icon (QUESTIONS 25, RESOLVED) was an unrelocated literal in
   `fn_17928`, and five more literal sites in `fn_173c4`, `fn_16f90` and main - bytes equal to the
   game's, fixups missing (GOTCHAS, first trap). Those sites are fixed. Iter 74: build + census
   `0 unexplained`, 3 racetests of ours and 4 of `WWORIG.EXE` - the race-start HUD (place 8,
   lives x3, faces x00, map, timer) is the original's, and the one grid that looked wrong is the
   original's own grid B (findings/game-loop.md `## Traps`); racetest now compares the race shot
   with both grids (0.00% / 1.23% floor). Then read in memory 3 s into the race, both builds
   (data object located by pattern vote; `dosrun.py locate` found no window in the hybrid):
   v7d884 -> 0x70660, v7d888 -> 0x6e728, v7d87c -> 0x70660, v7eb84 -> 0x7cbe8 as link addresses,
   the same in WW.EXE and the hybrid - CONFIRMED relocated. The players[] +0x50/+0x58 zeroing
   (fn_16f90) was not compared at runtime (race values move by then); fixcensus covers it statically.
   No long drive needed for these sites. `dosrun.py locate` now finds the hybrid too (iter 74: its
   sample points all fell in zero-filled data pages; data 0x60000 -> 0x22f000 live).
2. Re-run the behaviour sweep after every C change (81 lines, about 90 s, one process per function):
   `awk '$2 ~ /^0x/ && $4 !~ /^MATCH/ {print $3}' src/nearmiss.txt | while read n; do
   .work/venv/bin/python tools/wc/difffn.py $n; done`. Every DIFF is adjudicated (`ents_project`/
   `shots_project`/`net_input` = the dead `v7d7f4`/`v7d874` pointers, findings/game-loop.md;
   `scores_screen` = a stack address), so a new DIFF is news. So are nmrefs' classes: every
   `DIFFER`/`consts`/`jumps` line now carries a `BEHAV:` verdict.
3. 19 open lines still stop at the 3M-instruction cap and 11 at the 300-call cap: their waits spin with
   no library call, so `COUNT=`/`CALLS=` cannot help. Give `difffn.py` a `SEED=addr=value[b|w|d],...`
   knob (write the globals after the image load, then re-snapshot `init` so the watch re-run starts from
   the same seeded state) and take the three that crash on a null pointer global - `net_input` 0x2f4a8,
   `kart_draw` 0x24bcc, `fn_193a8` 0x193a8 - plus the menu spinners.
4. `com_wait_byte` 0x2df70 has no call site in the C module (0x10260-0x36a4d), so its EAX
   (0xffffffff/0x0) is unadjudicated: its caller is in the asm module above 0x36a4d, which difffn stubs.
   A whole-module dump has to be chunked - `tools/le/disasm.py` stops at the first undecodable byte
   (GOTCHAS).

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **207 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; iter 88 still 207 -
  no C change, `difffn.py`/`fncov.py` behaviour work only; iter 75 re-ran check.sh after the cars_menu behaviour fix: still 207; iter 74: check.sh also
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

1. `kart_bump` 0x22bf4: 55 of 606 instruction starts still unwalked after iter 88's fourteen SAME
   seed sets (strings in src/nearmiss.txt); biggest gaps 0x2374c-0x2376c, 0x231b8-0x231cf,
   0x22d2d-0x22d4a, 0x22c6a-0x22c7a, 0x234f0-0x23505, 0x22ef5-0x22f10.
2. 0x23746 `cmp bx,4; jb 0x23756` reads the **live** pickup record's `f20` and the function rewrites
   `p7d86c` itself: print `[0x7d86c]` at the gate (`.work/kbprobe.py`) before seeding. The f38 arm
   0x234f0-0x23505 wants `0x6aac8=2:w` with no `0x88be2=1:w` (the f18 arm zeroes EAX at 0x234be).
3. QUESTIONS 26: seed the palette/picture pointers `main` mallocs (src/ww.c 18540-18700) into
   scratch-page blocks and re-run `difffn.py intro` - it returns on both sides now, and its 123
   remaining DIFF bytes are all in the harness's zero page.
4. Wider walks of three functions closed earlier, each with a `fncov.py` count now that the tool is
   committed: `demo_mode` 0x1e2e4 (raise `CALLS=`/`COUNT=`; do NOT add
   `RESEED=0x88cd0=0:b`, the abort arm 0x31eae needs the press value on every read), `trial_book`
   0x2e4b8 with `TICKREAD=8` past 0x2f193, `race_run` 0x2fd8c with `SEED=0x7d998=2:d` alone.
5. After every `tools/link/build.sh`: `tools/link/fixcensus.py` must print `0 unexplained` (GOTCHAS,
   first trap); after a C change also re-run the behaviour sweep (`awk '$2 ~ /^0x/ && $4 !~ /^MATCH/
   {print $3}' src/nearmiss.txt | while read n; do .work/venv/bin/python tools/wc/difffn.py $n; done`)
   and compare its DIFFs with the verdicts in `src/nearmiss.txt` - the known uncapped classes are the
   dead `v7d7f4`/`v7d874` pointers, the page-0 scribble and a global holding a stack address
   (`palptr` 0x7d760, findings/menus.md); `CALLSEQ=1` names both stub-call sequences for a `calls n/m`.
6. Register homes (QUESTIONS 2): most of the 78 near misses differ only in which register or stack
   slot 9.5b picks; permute.py runs of 15 min gave 0-1 rows (iter 74). Not an overnight item.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **207 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; iter 85 still 207 -
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

1. QUESTIONS 27: seed the first branch of `kart_bump` 0x22bf4, `track_around` 0x283a4, `netkeys_in`
   0x2d94c, `line_points` 0x21554 - their zero-argument runs return in 29-52 executed instructions, so
   their `SAME` says nothing. Read each first `je` in the disassembly, seed what it tests (`minimap`
   0x2a4ac worked example: gate `v88ca0` 0x88ca0, `nplayers` 0x88c40, `netgame` 0x88c0c,
   `players[].x/y` 0x6aa90+0x16/0x18) and report the `hotfn.py` count with the verdict.
2. QUESTIONS 26: seed the palette/picture pointers `main` mallocs (src/ww.c 18540-18700) into
   scratch-page blocks and re-run `difffn.py intro` - it returns on both sides now, and its 123
   remaining DIFF bytes are all in the harness's zero page.
3. `leaderboard` 0x2b270 with `ARGS=` set to scratch-page blocks for its eight `short *` arguments
   (0x8000, 0x8080, ...): its 1673-insn zero-argument run is SAME but writes through 0, so the
   compared bytes are the scratch page it scribbles on, not the arrays it fills.
4. Wider walks of three functions closed earlier: `demo_mode` 0x1e2e4 (raise `CALLS=`/`COUNT=`; do
   NOT add `RESEED=0x88cd0=0:b`, the abort arm 0x31eae needs the press value on every read), `trial_book`
   0x2e4b8 with `TICKREAD=8` past 0x2f193, `race_run` 0x2fd8c with `SEED=0x7d998=2:d` alone.
5. After every `tools/link/build.sh`: `tools/link/fixcensus.py` must print `0 unexplained` (GOTCHAS,
   first trap); after a C change also re-run the behaviour sweep (`awk '$2 ~ /^0x/ && $4 !~ /^MATCH/
   {print $3}' src/nearmiss.txt | while read n; do .work/venv/bin/python tools/wc/difffn.py $n; done`)
   and compare its DIFFs with the verdicts in `src/nearmiss.txt` - the known uncapped classes are the
   dead `v7d7f4`/`v7d874` pointers, the page-0 scribble and a global holding a stack address
   (`palptr` 0x7d760, findings/menus.md); `CALLSEQ=1` names both stub-call sequences for a `calls n/m`.
7. Register homes (QUESTIONS 2): most of the 78 near misses differ only in which register or stack
   slot 9.5b picks; permute.py runs of 15 min gave 0-1 rows (iter 74). Not an overnight item.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

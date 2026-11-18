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

1. Behaviour night (overnight/prompt.md step 2): `tools/wc/nmrefs.py` now also lists `consts` -
   immediates and struct-field offsets only one side uses (15 of 79 near misses). Read the game's
   instruction for each, fix our C where it computes something else (fn_1832c and fn_16eac were real
   bugs that row counts hid); write `BEHAV:` into `src/nearmiss.txt`. MATCH count never drops.
2. Hybrid link (`tools/link/build.sh`, delinked data first so literal addresses hold): boots through
   every menu; with all our C it stalls at LOADING RACE (music ISR runs, main starved), with the 81
   near misses from the game (`ORIG=`) it reaches the race. Bisection on the fixed layout: see the
   commit after this one.
3. Near misses only with an idea their `differs:` note does not list (`tools/wc/permute.py`,
   MATCHING.md section 7); nmrefs after every change.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

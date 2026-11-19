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

1. `engines_menu` 0x167d8 is a known behaviour bug with no cause yet (QUESTIONS 24): `tools/wc/difffn.py
   engines_menu` ends with `spr_y` 0x52 in the game and 0x40 in ours, stable from a 1M to a 6M
   instruction cap, while `msgbox` (a MATCH) and `print_at` are each SAME run on their own. Find which
   row's y differs - the two last spr_y writes are in those helpers, so read the value engines_menu
   hands them (its `menu(2, 0x4e, 0, 0)` call site is byte-equal, so not the arguments). `scores_screen`
   ends with 0x7d760 = 0xe8/0xe4 and may be the same cause one level down.
2. Run the behaviour sweep after every C change (69 s for all 76 open lines, one process per function):
   `awk '$2 ~ /^0x/ && $0 !~ /MATCH/ {print $3}' src/nearmiss.txt | while read n; do
   .work/venv/bin/python tools/wc/difffn.py $n; done`. It found `players[].f14` (iter 69), a wrong
   stored value that check.sh, fndiff rows, and nmrefs' address and constant censuses all pass.
3. The f14 fix has not been seen on screen yet: the racetest overview is unchanged (RACETEST OK 0.0).
   `cpu_drive` 0x28cac walks the karts into their slots with `dir = +-f14` over the first frames of the
   race, so compare a capture taken *inside* the first race, not the overview.
4. 22 of the 76 open lines still have no behaviour verdict (14 hit the instruction cap, 8 the call cap,
   2 crash at the same instruction). Each needs its loop ended or its pointer globals seeded; try
   `COUNT=`/`CALLS=` first, they are env knobs of difffn.py.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

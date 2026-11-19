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

1. Behaviour night (overnight/prompt.md step 2), and now with a new tool: run
   `tools/wc/globrefs.py ww .work/src/WW.OBJ NAME 0xADDR` on a near miss and read every
   `ours only` / `game only` / `counts` line against the game's instruction. fn_173c4 gave up 3
   wrong-data-address bugs that way (the 0x7b238 table written at 0x7b230, two record fans reading
   faces+0x44/+0x60 instead of +0x3c/+0x58) with its fndiff row count unmoved at 100 - GOTCHAS "A
   data address of ours can be plain wrong". 11 of the 15 nmrefs-flagged near misses now carry
   `BEHAV:`; next in file order: fn_16f90 0x16f90 (`ours only #0x2d8`, a 728 we alone use),
   cars_menu 0x1a380 (`#0x178 #0x54 +0x78` ours vs `#0x140 #0x17c #0x1f #0x24 #0xb4` game - menu
   coordinates, the likeliest real bug of the rest), fn_1832c 0x1832c (`#0xe6` vs `#0xe8`).
2. Run globrefs over the whole near-miss list in one loop and triage the hits - it finds this bug
   class in MATCHed functions too, where fndiff has 0 rows. Expect address-shape noise (a global
   held in a register, a folded absolute base): the line is a question, not a finding.
3. Hybrid link (`tools/link/build.sh`): `tools/link/racetest.sh` = RACETEST OK (the first race's
   overview, identical to WW.EXE's). Next: drive into the race itself and compare frames - the
   0x7b238 animation table (findings/formats.md) is only read inside a race (`curkart+0x4e` set at
   0x3174a, walked by 0x2bff0), so iter 59's fan fixes cannot show at the overview; a `clipy`
   sprite clipped at row 199 (findings/video.md) needs the same.
4. QUEUE.md still holds only the unfilled template item (`Q1 <short title>`, empty Report/Accept),
   so iter 59 took the overnight prompt's step 2 instead; the human should put a real item there.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

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

1. Behaviour night (overnight/prompt.md step 2) with `tools/wc/nmrefs.py ww .work/src/WW.OBJ -v
   NAME=0xADDR`, reading every `game only` / `consts` line at the game's instruction: 15 of the
   nmrefs-flagged near misses now carry `BEHAV:`; iter 60 took fn_16f90 (no bug) and fn_1832c (no
   bug) and cars_menu, which gave up 2 bugs no row count saw (the third selection-box `drawsprite`
   0x1ae5d, and the caption drawn at x=0x2e where the game passes the centred capx 0x1a456-0x1a4a9/
   0x1a5c8 - our C computed capx and never used it, so 9.5b deleted the division; 476 -> 433 rows,
   RACETEST OK). Remaining without `BEHAV:`: enter_name 0x1cb44, main 0x35ca4, chat 0x14d00 - all
   three flag only `ours only ?int386x_` against `game only 0x43065`, which is the same clib
   `__int386x` wrapper (0x43065 pushes ESI/EDI/EBP, `mov ebx,ecx; mov esi,eax; mov edi,edx`, calls
   0x4c748) - confirm once, mark SAME - and then ents_project 0x25a78 (7 one-sided blocks, refs
   65/69).
2. Run globrefs (`tools/wc/globrefs.py ww .work/src/WW.OBJ NAME 0xADDR`) over the whole near-miss
   list in one loop and triage the hits - it finds this bug class in MATCHed functions too, where
   fndiff has 0 rows. Expect address-shape noise (a global held in a register, a folded absolute
   base): the line is a question, not a finding.
3. Hybrid link (`tools/link/racetest.sh`): still RACETEST OK (the first race's overview). Next: drive
   into the CHOOSE VEHICLE screen and compare it - iter 60's two cars_menu fixes (the centred caption,
   the selection box drawn a third time) show there and nowhere near the overview; the race frames
   need the same (the 0x7b238 animation table, a `clipy` sprite clipped at row 199).
4. QUEUE.md still holds only the unfilled template item (`Q1 <short title>`, empty Report/Accept),
   so iter 60 took the overnight prompt's step 2 again; the human should put a real item there.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

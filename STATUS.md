# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **207 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; iter 90 still 207 -
  no C change, `difffn.py`/`fncov.py` behaviour work only; iter 75 re-ran check.sh after the cars_menu behaviour fix: still 207; iter 74: check.sh also
  compiles `tools/link/extra.c`, whose `fn_1c068`/`fn_1c0ac` were always MATCH but never counted; objmatch
  compares rel32 branches between our own functions by target, which made demo_rec/pic_show/fade_out
  MATCH, `tools/wc/test_objmatch.py`), and iter
  66 checked what a MATCH masks inside those 202: all 4556 linker-filled operands point where the
  game's point (`tools/wc/fixsites.py`, 0 disagreements; `test_fixsites.py`). C covers
  all of 0x10260-0x36a4d (no gaps left, iter 56c); 0x36a4e on is the
  hand-written asm module.
- **Assembler module 0x36A4E-0x409EE rebuilt (iter 90)**: `src/wwasm.asm` (nasm, bootstrapped by
  `tools/asm/genasm.py`) assembles to the game's 40,865 bytes exactly; `tools/asm/asmcheck.py` (in
  `src/check.sh`; mutations in `test_asmcheck.py`) checks the bytes with -DBIN and the 1567 fixup sites
  of the OMF build. `tools/link/build.sh` links it instead of the game's bytes (`DELINK_ASM=0` = old
  way); RACETEST OK, and the linked asm in WWNEW.EXE is the game's bytes. Every routine named and
  commented (iter 91, findings/video.md); branch targets still `L_xxxxx`.
- Near misses: `src/nearmiss.txt` is the work list (ranked by `tools/wc/nearmiss.sh`; status
  column MATCH / SKIP: cause / LAYOUT / empty = open). Each function's causes and tried spellings
  are in its `differs:` note in `src/ww.c`.
  Iter 93s: netkeys_out/pack/in match (global used in place, no local): 210 of 285.
- What works and what does not: GOTCHAS.md "This project's traps" - *Reading a near-miss diff*,
  *Levers that produced MATCHes*, *Dead ends*. Do not copy levers into this file.
- Findings, evidence at the links: serial (findings/serial.md) rings 0x84b00/0x80afa, wrap 0x4000;
  classes (findings/classes.md) `v88bb2` is 1 AMATEUR..5 KID MODE **signed**; car select
  (findings/car_select.md) `ents[8]` 0x73998 is the box; scores (findings/scores.md) `tbl77758`;
  disc files (findings/formats.md) WACKY.DAT = 577 records of 22 bytes, offsets from 2, 42 sets of
  per-track files.

## Next

1. The asm is read and named (iter 91, findings/video.md + input.md; 33 data names, names file
   "was"). Left: names for the loops worth naming.
2. Sound library: src/ass is ASS at Wacky Wheels' revision (iter 93; src/ass/README.md), built with
   Watcom 10.0a/10.0 prerelease, `tools/ass/asscheck.py` 276 of 285 named functions, 16 modules whole (TASK_MAN, PCFX, SNDSRC, DMA, GUS, GUSWAVE, PITCH, FX_MAN, MUSIC, MULTIVOC, FMSONG
   rewritten: findings/executables.md; `tools/ass/portnames.py` names their symbols). Near misses: MIDI 324 bytes, GUSMIDI 140, BLASTOLD 48, PAS16 44, FMCHIP's FM_SetChannelTimbre (register choices, scheduling, spills). Every sound-library module the game links now has source (the KLM player rebuilt from the binary as FMSONG/FMCHIP, iter 93n).
3. `trial_book` 0x2e4b8 with `show` != 0 (iter 89 opened it): seed `laptimes` 0x7d358 (five entries,
   `cmp edx,5` 0x2e6b2) and NUL-terminated name strings in the 0x78-byte records at 0x70730, then the
   record block behind `newlap` (stored only at 0x2f076) is walkable and the text loop 0x149df-0x14af3
   stops eating the instruction budget.
4. `kart_bump` 0x22bf4: 55 of 606 instruction starts unwalked after iter 88. The f38 arm 0x234f0-0x23505
   wants `0x6aac8=2:w` with no `0x88be2=1:w` (the f18 arm zeroes EAX at 0x234be); 0x23746 reads the
   **live** pickup record's `f20`, so print `[0x7d86c]` at the gate before seeding (`.work/kbprobe.py`).
5. `demo_mode` 0x1e2e4 with its recorded seeds and raised `CALLS=`/`COUNT=` (iter 84's run stops at
   CALLS=6000), plus `fncov.py` for its coverage number. Do NOT add `RESEED=0x88cd0=0:b` - the abort arm
   0x31eae needs the press value on every read.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

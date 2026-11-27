# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **215 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; 2026-11-27 +2 ents_show, fn_1be3c; iter 100 +1 line_draw;
  iter 97 +1 fade_in; iter 94 +1 shots_clear; iter 93s +3; iter 90 +2 with no C change (`difffn.py`/`fncov.py` behaviour work);
  iter 74 started counting `tools/link/extra.c`; objmatch compares rel32 branches between our own
  functions by target, `tools/wc/test_objmatch.py`). Iter 66 checked what a MATCH masks: all 4556
  linker-filled operands point where the game's do (`tools/wc/fixsites.py`, 0 disagreements). C
  covers all of 0x10260-0x36a4d (no gaps left, iter 56c); 0x36a4e on is the hand-written asm module.
- **Assembler module 0x36A4E-0x409EE rebuilt (iter 90)**: `src/wwasm.asm` (nasm, bootstrapped by
  `tools/asm/genasm.py`) assembles to the game's 40,865 bytes exactly; `tools/asm/asmcheck.py` (in
  `src/check.sh`; mutations in `test_asmcheck.py`) checks the bytes with -DBIN and the 1567 fixup sites
  of the OMF build. `tools/link/build.sh` links it instead of the game's bytes (`DELINK_ASM=0` = old
  way); RACETEST OK. Every routine named (iter 91, findings/video.md); branch targets `L_xxxxx`.
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

- **Port (port/, iter 105)**: `port/build.sh` builds `.work/port/wacky` (32-bit, SDL2) from src/ww.c
  unchanged (port/tools/prep.py), src/wwasm.asm translated by port/tools/asm2c.py, and a runtime that
  maps WW.EXE's data object at 0x60000 and emulates the VGA, timer tasks, keyboard and DOS services
  (port/README.md). Check: `port/check.sh` -> `PORTCHECK OK` (title screen pixel-identical to the
  original's; iter 106: racetest's key path reaches ENGINES and the track overview as the original does,
  QUEUE Q2); mutations: `plane[x & 3]` -> `plane[(x + 1) & 3]` in vga.c's render, and `io_cost` returning
  at once, each make it FAIL. Music runs through the game's own FM song player into Nuked OPL3, and the
  sound effects through FX_MAN + MULTIVOC (`port/check.sh` step 3; see Next). Timing model: port/README.md "Timing".

## Next

History of the items below is in git (`git log --grep 'iter 10[4-6]'`) and in the findings files.

1. Sound library (src/ass, ASS at Wacky Wheels' revision, built with the **Watcom 10.0 release**,
   iter 105): `tools/ass/asscheck.py` 283 of 285 functions, 19 modules whole. Left: MIDI_PlaySong
   (39 rows) and FMCHIP's FM_SetChannelTimbre (22 rows, DL/EDX vs AL/EAX), both register allocation.
   The game's own C is 9.5b beyond doubt (iter 105 built it with every Watcom here).
2. C near misses: the offline list is closed (iter 104: the 15 register-home functions are each
   `MATCH` or `VB:`; the eight 2-row lines are the emission-order class, GOTCHAS). Untested leads from
   other Watcom projects: QUESTIONS "Leads from other Watcom matching projects".
3. `src/wwasm.asm`: every routine and branch target named (iters 91, 104, 105), except the
   compare-chain arms of `rect_overlap` 0x36d69 and `surface_at_7c` 0x37a6c (`.L<address>` on purpose).
4. Behaviour coverage (`tools/wc/difffn.py`, iter 105): trial_book 1006/1034 starts, kart_bump
   603/606, demo_mode 189/221 (second-race path out of reach), all SAME; of the 72 non-matching
   functions 68 are SAME with the default env (`BUCKET=1` buckets a DIFF; findings/game-loop.md).
   Open: `net_input` coverage (55 of 203 starts), `race_run` (784 of 2028).
5. Port (iter 106; QUEUE Q1, Q2 ready for review): title, menus, race (600 frames, step 4) and race
   with player input (800 frames, step 5) equal the original's; music (Nuked OPL3, corr 0.945) and
   effects (FX_MAN + MULTIVOC, 0.956); rand equals WW.EXE's under Unicorn; every menu, cup, time trial,
   two-player modes, duck shoot and kid mode driven without a crash (findings/game-loop.md "Reads
   through NULL"). Not driven: Comm-Bat. Next candidate: a QUEUE item from the human.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), the
  EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

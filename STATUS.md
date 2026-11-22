# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **213 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; iter 100 +1 line_draw;
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
  at once, each make it FAIL. Music runs through the game's own FM song player (no OPL2 yet: silent);
  sound effects are stubs. Timing model: port/README.md "Timing".

## Next

0. Iter 104 named all 61 asm-module loop headers (`.kind_<address>`, findings/video.md; bytes and fixups
   unchanged) and closed the C module's offline near-miss list: the 15 register-home functions are every
   one `MATCH` or `VB:`, and the eight 2-row lines are the emission-order class (GOTCHAS).
1. Sound library: src/ass is ASS at Wacky Wheels' revision (iter 93; src/ass/README.md). Iter 105
   found the game's compiler: the **Watcom 10.0 release** (5/1994, `WC100R`, run under Wine), one
   compiler for every module. `tools/ass/asscheck.py` 283 of 285 named functions, 19 modules whole
   (findings/executables.md). Two near misses left, both register allocation: MIDI_PlaySong
   (39 rows; declaration orders and four header-read spellings tried) and FMCHIP's
   FM_SetChannelTimbre (22 rows, DL/EDX vs AL/EAX; the tried spellings are in its FMCHIP.C note).
   The game's own C is 9.5b beyond doubt: iter 105 built it with every Watcom here (9.5, 9.5a, 10.0
   prerelease, release, 10.0a, 10.0b; 9.5c in iter 93p) and none matches a function 9.5b misses.
2. `src/wwasm.asm` branch targets are all named (iter 105, findings/video.md), except the compare-chain
   arms of `rect_overlap` 0x36d69 and `surface_at_7c` 0x37a6c, which keep `.L<address>` on purpose.
3. `trial_book` 0x2e4b8 is walked (iter 105: 1006 of 1034 starts, every record outcome SAME; findings/menus.md).
   Since iter 105 `tools/wc/difffn.py` stubs the hybrid's `WWASM_TEXT` too (GOTCHAS).
4. `kart_bump` 0x22bf4 is walked (iter 105: 603 of 606 starts, the 3 left unreachable; findings/game-loop.md).
5. `demo_mode` 0x1e2e4 is walked (iter 105: modes 1-3 SAME, 189 of 221 starts; the second-race path needs a
   race that ends without the abort, out of the harness's reach; findings/menus.md).
6. DIFF-bucket pass done (iter 105): of the 72 non-matching functions, 68 are SAME with the default
   env. The 4 DIFFs are the dead `v7d874`/`v7d7f4` stores and `palptr`'s stack address; `net_input`
   needed a seed for `p7e728`. `difffn.py BUCKET=1` buckets any DIFF (findings/game-loop.md). Open:
   `net_input` coverage (55 of 203 starts), `race_run` (784 of 2028).
7. Port (iter 106, QUEUE Q2 ready for review): menus to the first race match the original's. Next
   candidate: the race itself against a deterministic reference. Done iter 106: sound effects -
   FX_MAN + MULTIVOC as rebuilt over the port's SB16 (blaster.c), mixed with the OPL; the Apogee
   recording now scores 0.956 with the logo's voice (QUESTIONS.md 30: the original's 16-bit data is
   unsigned though the DSP is told signed). Fixed 2026-11-22: finishing a race 1st-3rd crashed the
   port - fn_173c4 (src/ww.c) left the win animation's face frames 0x7b38c.. empty (loop cursor
   restarted; game 0x1762f); the leader board and the next race now follow. Also 2026-11-22: the hall of fame's stamp is today's
   date (0x43164 is _dos_getdate, INT 21h/2Ah - findings/scores.md had 2Ah/2Ch swapped, and the port
   stamped "31 JUN 3370"); a forced cup (13 races 1st, the final, rostrum, name entry) and forced
   4th places (TRY TO QUALIFY, lives, GAME OVER) run through without a crash. Then time trial
   (a NULL surface read the original makes too: port_lowmem), two-player race and shoot-out
   (cars_menu's turn tests were inverted: fixed, 445 -> 444), duck shoot and kid mode, each driven
   with forced finishes - no crash left (findings/game-loop.md "Reads through NULL").
   Done 2026-11-22: the race against the original - port/check.sh step 4 compares 600 race frames
   of all eight player records with captures/race_trace_ww.bin (SAME; 1201 frames checked once;
   mutation in cpu_drive fails at frame 35). Next: the same with player input (needs input the
   harness can give at a known frame) and the karts[] records. Done iter 106: music - Nuked OPL3 behind FMCHIP's
   0x388, SDL audio or PORT_WAV; port/check.sh step 3 compares the Apogee song with the original's
   recording (captures/apogee_ww.wav, `tools/dosrun.py wave start|stop`): corr 0.945, mutation 0.848. Done iter 106: the port's rand/srand
   (port/src/wrand.c, seed at the game's 0x64008) equal WW.EXE's under Unicorn,
   `port/tools/checkrand.py` -> RANDCHECK OK (14 seeds + unseeded x 2000 draws; mutation 0x3039 ->
   0x3038 fails it).

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

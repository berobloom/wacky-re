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

1. Show the two fixes on screen: neither is in the racetest path (both ended RACETEST OK 0.0, the
   overview unchanged). `menu` 0x155c0 now draws 'LEE & JACKSON LAWNMOWERS' at the caller's y (0x15fad,
   `engines_menu` passes 0x4e at 0x168cd) and iter 69's `players[].f14` moves the karts in the first
   frames of the race (`cpu_drive` 0x28cac). Smallest step: a second screenshot in
   `tools/link/racetest.sh` on the Options -> Engines screen, and one taken inside the first race.
2. Re-run the behaviour sweep after every C change (now 81 lines - the filter picks the status column,
   not the whole line - about 90 s, one process per function): `awk '$2 ~ /^0x/ && $4 !~ /^MATCH/
   {print $3}' src/nearmiss.txt | while read n; do .work/venv/bin/python tools/wc/difffn.py $n; done`.
   Every remaining DIFF is now adjudicated: `ents_project`/`shots_project`/`net_input` are the dead
   `v7d7f4`/`v7d874` past-the-end pointers (findings/game-loop.md), `scores_screen` is a stack address.
   A new DIFF is therefore news.
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

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

1. The address/const/count census class is closed on both halves. All 21 nmrefs candidates carry
   `BEHAV:`, and the near-miss half (20 one-sided items) is done; what is left of it is 35 switch-table
   entries the byte census cannot decide, hand-check them with
   `tools/wc/fixsites.py ww .work/src/WW.OBJ --arms NAME`: the tables at 0x1a360 (8 entries, arms of
   `cars_menu`), 0x24340 (9, `kart_fire`), 0x1f824 (12 more, `main_menu`) and 0x2e4a0 (6, `trial_book`)
   - QUESTIONS 22. Iter 66 did the MATCH half: 4556 sites in the 202 matched functions, 0 disagree (no
   matched function reads another global, calls another function or passes another literal), and two
   tables resolved same-shape (`kart_frame`'s 14 arms with iter 62's check, `fn_19db8`'s 4; the one
   entry that leaves its function, `main_menu` 0x1f844 -> `engines_menu`'s epilogue 0x16978, is the
   game sharing a tail).
   Iter 65 also ran `tools/wc/globrefs.py ww .work/src/WW.OBJ NAME 0xADDR` over all 102 lines of
   src/nearmiss.txt (after fixing that tool's wrong bodies and its half-read operands): 20 near misses
   still show a one-sided non-string item and all 20 carry `BEHAV:`; the last fresh one, showlines
   0x2a268, is SAME. The LAYOUT trio (demo_rec 0x2e318, pic_show 0x32c98, fade_out 0x1bf94): every
   cross-region jump lands at the same intra-function offset as the game's, only the displacement
   differs. Ignore `game only 0x6xxxx` string lines (GOTCHAS).
2. Screen-level is the only lever left: `tools/link/racetest.sh` last ran RACETEST OK (iter 60) and
   iters 61-66 changed no executable code, so none was run - run it once at the next code change,
   then drive into CHOOSE VEHICLE and compare, then the race frames (the 0x7b238 animation table, a
   `clipy` sprite clipped at row 199, findings/video.md).
3. `tools/wc/nearmiss.sh` (~5 min) is overdue and its first column drifts both ways: fn_17928 read 7
   in the file and 25 on the committed code, 15 of those one whole-frame swap of numbuf/msg (a BLOCKED
   class - re-measure, do not chase). Iter 65 corrected fn_17928's column only.
4. QUEUE.md still holds only the unfilled template item (`Q1 <short title>`, empty Report/Accept), so
   iters 61-66 took the overnight prompt's step 2 again; the human should put a real item there.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

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

1. Behaviour night (overnight/prompt.md step 2): 6 of the 15 nmrefs-flagged near misses now carry
   `BEHAV:` in `src/nearmiss.txt` (iter 57: players_ahead's clipy was a real bug, findings/video.md;
   engines_menu, kart_state, pic_at, scores_screen, menu are frame-size/slot/register shape only).
   Next in nmrefs file order: ent_sprites_load 0x21310 (`game only 6x in sprattr`, the i*40 strength
   reduction, QUESTIONS 2), fn_193a8 0x193a8 (`game only 1x pal_6bit` - we call the aux alias),
   fn_173c4, fn_16f90, players_project (#0x8), ents_project, shots_project (+0x22), cars_menu 0x1a380,
   fn_1832c 0x1832c.
2. Hybrid link (`tools/link/build.sh`): all our C + the game's library and data. Known state:
   `tools/link/racetest.sh` = RACETEST OK (the first race's overview, identical to WW.EXE's). Next:
   drive into the race itself and compare frames (the racetest stops at the overview) - a sprite
   clipped at row 199 (`clipy`, findings/video.md) only shows inside a race.
3. The clipy class, once more: a global read a second time *after* a store that used the same
   expression changes meaning (players_ahead). The correct spelling keeps the value in a local -
   `n = spr_w - 1 + spr_y - 0xc7; spr_w -= n; ... v7d878 = n * 4;` (src/ww.c:11353, drawsprite_clip
   0x11404); check the other sprite-draw sites against the game's `mov [g],reg` after its `sub`.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

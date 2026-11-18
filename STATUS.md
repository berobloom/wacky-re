# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **198 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`). C covers
  0x10260-0x36a4d bar the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is the
  hand-written asm module.
- Near misses: `src/nearmiss.txt` is the work list (ranked by `tools/wc/nearmiss.sh`; status
  column MATCH / SKIP: cause / LAYOUT / empty = open). Each function's causes and tried spellings
  are in its `differs:` note in `src/ww.c`.
- What works and what does not: GOTCHAS.md "This project's traps" - *Reading a near-miss diff*,
  *Levers that produced MATCHes*, *Dead ends*. Do not copy levers into this file.
- Findings, evidence at the links: serial (findings/serial.md) rings 0x84b00/0x80afa, wrap 0x4000;
  classes (findings/classes.md) `v88bb2` is 1 AMATEUR..5 KID MODE **signed**; car select
  (findings/car_select.md) `ents[8]` 0x73998 is the box; scores (findings/scores.md) `tbl77758`.

## Next

1. Near misses: no open status in `src/nearmiss.txt` (QUEUE.md's only item is the unfilled
   `Q1 <short title>` template - not actionable, iters 46-47). Next leads from `nmrefs.py
   ww .work/src/WW.OBJ` (`game more`/`ours more` = a local copy or an extra read, not noise):
   chat 0x14d00 (**`ours only ?int386x_`**, plus one extra spr_y read), kart_fire 0x24364
   (12 more v7eb9c reads), players_ahead 0x34208. nmrefs after every change; re-rank with
   `tools/wc/nearmiss.sh` (its first column is stale).
2. After a MATCH: rerun `tools/progress.py` (writes PROGRESS.md, byte-identical functions per part
   and per subsystem of `analysis/ww_subsystems.txt`; src/ww.c stays one file); docs/media pictures
   come from the original (DOSBox-X states `mainmenu`, `trackpick` in .work/dos/states; accelerate is
   Alt). Before probing, run
   the GOTCHAS scans on the game's code: a
   cached global (`mov [G],r` up to 12 instructions before a call, `mov r,[G]` after it: compute in
   the global), a dword slot reloaded `movsx word` (an int passed to a short parameter), a `je` to
   the epilogue (an early `return`), raw `WS(0x...)` addresses (use the named fields).
   `tools/wc/permute.py NAME ADDR END [ROUNDS COPIES SEED]` (random-mode permuter, 30 copies a
   compile) found shot_fire's last swap in one round; it is worth a 20-round run once a function is
   down to scheduling rows.
3. Take a SKIPped near-miss line only with an idea its `differs:` note does not list: a row that
   differs only in the operand order of one `cmp`, and a register-home row, are the tie-break class
   (GOTCHAS "Reading a near-miss diff" / "Dead ends").
4. Re-try the newest levers on SKIPped lines whose cause is a register home or a hoisted load:
   split arithmetic `x = <expr>; x *= k;` and the braced 0/1 if/else, e.g. `pcx_load`, `net_recv`, `shots_move`;
   `intro` 0x348f4 for the `= .*== 0` assignment. (`(int *)charbase + i * 2`: view_at_curpos' head
   and the view_box/cpu_drive sites already match - the idea is spent, in_sector's rows are the
   leading-block rotation.)
5. The two C gaps: 0x1f824-0x1f857 (small, between two rebuilt functions) and the fade-out
   0x1bf7e-0x1c0f3 with its out-of-line tail at 0x32cc1-0x32cf7 (`pic_show`'s padding diff).
6. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation, a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

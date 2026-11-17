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

1. Near misses: the first line with an empty status in `src/nearmiss.txt` (procedure:
   overnight/prompt.md step 2). Next open: `volume_menu` 0x01e760 (64), `fn_1c0f4` 0x01c0f4 (67),
   `leaderboard` 0x02b270 (73). Before probing, run the GOTCHAS scans on the game's code: a
   cached global (`mov [G],r` up to 12 instructions before a call, `mov r,[G]` after it: compute in
   the global), a dword slot reloaded `movsx word` (an int passed to a short parameter), a `je` to
   the epilogue (an early `return`), raw `WS(0x...)` addresses (use the named fields).
   `tools/wc/permute.py NAME ADDR END [ROUNDS COPIES SEED]` (random-mode permuter, 30 copies a
   compile) found shot_fire's last swap in one round; it is worth a 20-round run once a function is
   down to scheduling rows.
2. Near misses just below the top: `com_open` 0x102f4 is 13 rows (all register homes) and
   `shot_step` 0x25e6c, `fn_182d0` 0x182d0 are 2-3 rows - only take them with an idea the
   `differs:` note does not list. `time_str` 0x2a618 (31) and `voices_load` 0x11f98 (33) are now
   SKIPped; their frame and fixup-row traps are in the notes.
2. Re-try the newest levers on SKIPped lines whose cause is a register home or a hoisted load:
   the `(int *)charbase + i * 2` spelling of every `(int *)(v7d6a0 + curpos * 8)` site
   (`view_at_curpos` 0x2f408, `view_box`, `in_sector`, `cpu_drive`), split arithmetic
   `x = <expr>; x *= k;` and the braced 0/1 if/else, e.g. `pcx_load`, `net_recv`, `shots_move`;
   `intro` 0x348f4 and `sounds_load` 0x12124 for a `= .*== 0` assignment.
3. The two C gaps: 0x1f824-0x1f857 (small, between two rebuilt functions) and the fade-out
   0x1bf7e-0x1c0f3 with its out-of-line tail at 0x32cc1-0x32cf7 (`pic_show`'s padding diff).
4. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation, a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

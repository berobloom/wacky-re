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

1. Near misses: no open status in `src/nearmiss.txt`; iter 46 took fn_1c0f4 14 -> 8 rows and to the
   game's size (a temp `t = (int)strlen(rows[10]) * 8;` before the centred line puts the `lea`
   before the constant and sinks the print_at mode too; the split for-init `for (i = 1, q = rows[0],
   q += 80; ...)` gives `mov edi,esp; add edi,0x50` where one expression gives a `lea`). Its two
   remaining blocks are the scheduling/coin-flip classes. Next leads, from
   `nmrefs.py ww .work/src/WW.OBJ` (`game more`/`ours more` = a local copy or an extra read, not
   noise): chat 0x14d00 (**`ours only ?int386x_`** - we call an external the game does not, plus one
   extra spr_y read), kart_fire 0x24364 (game has 12 more v7eb9c reads), ents_project 0x25a78 (6 more
   v7d7f4), players_ahead 0x34208 (v7d800 promoted in ECX there, not here). Re-rank with
   `tools/wc/nearmiss.sh` - its first column is stale on several lines. nmrefs after every change.
2. README and progress (iter 38, done): src/ww.c stays one file; `tools/progress.py` writes
   PROGRESS.md (byte-identical functions per part and per subsystem of `analysis/ww_subsystems.txt`);
   rerun it after MATCHes. Pictures in docs/media from the original (DOSBox-X states `mainmenu`,
   `trackpick` in .work/dos/states; accelerate is Alt). Before probing, run the GOTCHAS scans on the game's code: a
   cached global (`mov [G],r` up to 12 instructions before a call, `mov r,[G]` after it: compute in
   the global), a dword slot reloaded `movsx word` (an int passed to a short parameter), a `je` to
   the epilogue (an early `return`), raw `WS(0x...)` addresses (use the named fields).
   `tools/wc/permute.py NAME ADDR END [ROUNDS COPIES SEED]` (random-mode permuter, 30 copies a
   compile) found shot_fire's last swap in one round; it is worth a 20-round run once a function is
   down to scheduling rows.
3. Near misses just below the top: `fn_1c0f4` 0x1c0f4 (8 rows) and `draw_status` 0x12f80 (17) are the
   smallest with named non-home causes - draw_status: print_at's `mov ebx,esp`/`xor ecx,ecx` hoisted
   into the inline strcpy's setup at every site, and only the SECOND `netgame == 1` pnames pair
   merges (the first is byte-identical, so a spelling must not touch it); `showlines` 0x2a268 (18) is
   10 slot rows plus 2 `mov ax,word[spr_x]` vs the game's `movsx eax` (width rule; plain `int x/y`
   tried, a mixed `short`/`int` pair was not). `com_open` 0x102f4 (13), `shot_step` 0x25e6c and
   `fn_182d0` 0x182d0 (2-3) - only with an idea the `differs:` note does not list.
4. Re-try the newest levers on SKIPped lines whose cause is a register home or a hoisted load:
   the `(int *)charbase + i * 2` spelling of every `(int *)(v7d6a0 + curpos * 8)` site
   (`view_at_curpos` 0x2f408, `view_box`, `in_sector`, `cpu_drive`), split arithmetic
   `x = <expr>; x *= k;` and the braced 0/1 if/else, e.g. `pcx_load`, `net_recv`, `shots_move`;
   `intro` 0x348f4 and `sounds_load` 0x12124 for a `= .*== 0` assignment.
5. The two C gaps: 0x1f824-0x1f857 (small, between two rebuilt functions) and the fade-out
   0x1bf7e-0x1c0f3 with its out-of-line tail at 0x32cc1-0x32cf7 (`pic_show`'s padding diff).
6. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation, a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

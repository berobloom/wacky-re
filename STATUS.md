# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **191 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  `src/nearmiss.txt` entries (10 MATCH, 22 SKIP, 2 LAYOUT, 66 open); C covers 0x10260-0x36a4d bar
  the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is the hand-written asm.
- Compiler levers that worked (GOTCHAS "This project's traps" lists them with their evidence): an
  `int` local assigned inside one arm, a second local re-armed at the loop body's end, the pointer
  step of a `for` tail written in the body, `volatile` to pin a store 9.5b defers, **`volatile` on a
  local to get the game's `sub esp,4` + `[esp]` spill/reload of a temp** (shot_step, iter 23), one
  local for two switch arms, the operator assignment for a whole-register shift; a temp declared
  **before** both consumers of a shared base, and `d = q + q` for `add eax,eax` where `q * 2` gives
  `lea`; a word-typed second C name for a global the game truncates through a *word* load; an
  assignment the call does not read belongs **below** that call (net_key); the load of the *next*
  statement **above** the assignment it splits (fn_16ca0); **moving one arm's copy of a statement
  that ends both arms to the head of its arm un-merges the tail-merge without touching the branch**
  (race_records_menu, iter 24, 13 -> 12 rows).
- Not reachable so far (each has its `differs:` note and a SKIP line with the spellings spent): the
  home of one or two locals (net_recv, fn_1be3c, pcx_load, com_wait_byte, com_expect, self_project,
  shots_clear, view_at_curpos, fade_in's init-loop i/k), a dead incoming-register copy (fn_182d0),
  one moved load (track_setup, music_jingle, demo_play), the v88bb2/v7edb0 pair (classes_menu), the
  int shift into `err` (line_draw), two load orders (shot_step, 2 rows left), and the scheduling of a
  straight-line head (in_sector, iter 25: the game puts its `test` above four table loads and keeps
  each index/load pair adjacent).
- Probe caution (GOTCHAS iter 19, 21, 22, 24) and `fndiff.py`'s fake rows: a `[reg +/- @]` pair whose
  displacement is a fixup is the 0x90 mask filler - 7 of `self_project`'s 10 rows, 3 of
  `fn_1be3c`'s 13, 2 of `shots_clear`'s 13. A variant can also score *fewer* rows by matching less
  (a dropped `push ebx` + `mov ebx,0x7f2b0` shrinks the window and stops the rows pairing up; a
  2-byte-shorter body also drops the padding row, in_sector's `is_idxloc` 15 -> 14): read the rows.
  `src/check.sh` (the byte compare) is the verdict.
- Findings, with evidence at the links: serial (findings/serial.md) `com_isr`/`com_isr_fifo`
  byte-identical, one `short` byte local in both arms, rings `rxbuf` 0x84b00 / `txbuf` 0x80afa, wrap
  0x4000, and `v88cae` = low byte of `v7ed8c` (0x2dd42-0x2dd4e); classes (findings/classes.md)
  `v88bb2` 1 AMATEUR..5 KID MODE **signed**, release check 0x34463-0x344d4; car select
  (findings/car_select.md) `ents[8]` 0x73998 is the selection box; scores (findings/scores.md)
  `tbl77758` rows of 0x450 = 24 records of 0x2e; menus (findings/menus.md) `fn_182d0`'s 4th argument
  is a **word** (0x182e9 `test cx,cx`); sound (findings/sound.md unwritten) `music_start` inlined by
  the game; video (findings/video.md) `setpalette` is an asm `rep movsb` of 0x3e80 bytes, page flips
  are `if`/`else` pairs of stores, `pal_shift` 0x114a8 (identical) has no caller (QUESTIONS 15).

## Next

1. `netkeys_in` 0x2d94c (29 rows): the same `c` in a saved EDX as netkeys_out/netkeys_pack - write
   `SKIP:` with 0 variants (BLOCKED below), then take the first open line.
2. `net_input` 0x2f4a8 (15 rows, no note): `tools/wc/fndiff.py ww .work/src/WW.OBJ net_input
   0x2f4a8`; its sibling net_recv 0x2db00 is a BLOCKED crc-home case, so read the rows first and only
   probe if the cause is not a register home.
3. `kart_move` 0x237e4 (20 rows): its note says the same `[esp]` slot as shot_step - apply the
   `volatile int t` lever there; shot_step itself stopped at 2 rows (see BLOCKED).
4. `race_records_menu` 0x1d13c is at 12 rows: 5 are the EDX-vs-AL home of the pooled "" byte (see
   BLOCKED), 4 the un-merge spelling now in `src/ww.c`; decide whether to keep that spelling or go
   back to the merged one before spending more variants.
5. `pal_shift` 0x114a8: scan the LE **fixup records** (not the applied bytes) and any far-pointer
   table for 0x114a8 - dead code vs computed call - and say what the 768 bytes are (QUESTIONS 15).
6. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer; the original under the harness (LE deltas, a save state).

## BLOCKED (dead-end classes, do not re-probe)

- Stack-slot permutation of locals (`release_check` 0x34458, `wtext` 0x146d4, `enter_name`,
  `scores_screen`, `fn_1c0f4`): Watcom's scalar slot order is neither declaration nor use order.
- Two locals' register pairing - a tie-break, not a translation error (`pcx_load` ESI/EDI,
  `com_wait_byte` EBX/EDX + EAX/ECX, `com_expect` EBX/ESI + the A1 vs 8B 0D size it forces,
  `self_project` EAX/EDX in the clip sum, `shots_clear` EBX/EDX for pointer and counter, `fade_in`'s
  init-loop EDX/EBX): 24, 16, 10, 17 and 21 spellings move nothing. Two pushed-register homes are as
  unpinnable as the EAX one, and the order of the initializers in a comma `for` only moves the order
  of the two zeroing `xor`s (iter 25).
- One local's home and constant hoisting - the same tie-break at one variable (`netkeys_out`,
  `netkeys_pack`, `netkeys_in`: c in a saved EDX vs EAX; `fn_1be3c`: the constant 0 in a pushed EDI
  vs `xor ebx,ebx`/`cmp word [g],0`; `race_records_menu`: the pooled "" byte in EDX vs AL); 9.5b has
  **no explicit register variables in any scope** (function scope E1009, file scope E1059), so it
  cannot be pinned from the declaration. A DL home also explains the size gap: AL owns the short
  forms (netkeys_pack 0x2da45-0x2da85 six rows, 0x2da2b/0x2da88 two), so count encodings before
  hunting a missing statement.
- Instruction scheduling inside one straight-line head (`in_sector` 0x24a08): 9.5b interleaves two
  independent index/load chains where the game keeps the pairs adjacent, and a statement written
  **below** a branch sinks past the whole branch - it never lands between the branch's `test` and its
  `jcc`, so a load sitting in a game branch shadow is not a source position (iter 25, 10 spellings).
  Watcom also does not CSE a global array read across separate conditions.
- SKIP at once (0 variants): the classes above and a global the game keeps in a register across a
  loop (QUESTIONS 2). SKIPped: music_jingle, track_setup, classes_menu, fn_182d0, line_draw, net_recv,
  pcx_load, demo_play, view_at_curpos, wtext, release_check, com_wait_byte, com_expect, self_project,
  netkeys_out, netkeys_pack, fn_1be3c, shot_step (2 rows: one 2-instruction load-order swap),
  shots_clear, fade_in, in_sector.

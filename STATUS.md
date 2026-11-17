# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **191 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  `src/nearmiss.txt` entries (10 MATCH, 16 SKIP, 2 LAYOUT, 72 open); C covers 0x10260-0x36a4d bar
  the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is the hand-written asm.
- Compiler levers that worked (GOTCHAS "This project's traps" lists them with their evidence): an
  `int` local assigned inside one arm, a second local re-armed at the loop body's end, the pointer
  step of a `for` tail written in the body, `volatile` to pin a store 9.5b defers, **`volatile` on a
  local to get the game's `sub esp,4` + `[esp]` spill/reload of a temp** (shot_step, iter 23), one
  local for two switch arms, the operator assignment for a whole-register shift; a temp declared
  **before** both consumers of a shared base, and `d = q + q` for `add eax,eax` where `q * 2` gives
  `lea`; a word-typed second C name for a global the game truncates through a *word* load; an
  assignment the call does not read belongs **below** that call (net_key); the load of the *next*
  statement **above** the assignment it splits (fn_16ca0).
- Not reachable so far: a loop-carried local's home (net_recv), a dead incoming-register copy
  (fn_182d0), one moved load (track_setup, music_jingle), the v88bb2/v7edb0 pair (classes_menu), the
  int shift into `err` (line_draw), ESI vs EDI (pcx_load), one copy late (demo_play), a `short +
  short` sum's first operand (view_at_curpos), `com_wait_byte`/`com_expect`'s three register homes,
  `self_project`'s 3 real rows, the order of two loads in shot_step (2 rows left), and a lone byte
  local's home: the game keeps `c` in a **saved** EDX (`push edx`) where 9.5b homes it in EAX - 21
  spellings and 9.5/9.5a/9.5b all give EAX, and 9.5b has no explicit register variables
  (`register char c @ edx` is E1009).
- Probe caution (GOTCHAS iter 19, 21, 22) and `fndiff.py`'s fake rows: a `[reg +/- @]` pair whose
  displacement is a fixup is the 0x90 mask filler - 7 of `self_project`'s 10 rows, 3 of
  `fn_1be3c`'s 13. `src/check.sh` (the byte compare) is the verdict.
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

1. `shot_step` 0x25e6c is 2 fndiff rows from a MATCH: the game reloads `t` from `[esp]` into EBX
   (0x25f46) *before* loading `v7d6a0` into EAX (0x25f49), ours the other way round; `p = t;` first
   gets the order but gives `p` EAX and the base EBX. Try routing the base through a local that
   already lives in EAX (the songs_load lever) before logging it as the tie-break.
2. Apply the `volatile`-temp lever to the functions whose notes say the game has a frame slot ours
   lacks: `kart_move` 0x237e4 (`t = curpos * 8` + `*v7d690--`, 20 rows) and any entry whose game
   prologue is `push ... ; sub esp,4`. Check the prologue first (GOTCHAS iter 20).
3. Mark the rest of the `c in DL` net family SKIP at once (0 variants): `netkeys_pack` 0x2da28 and
   `netkeys_in` 0x2d94c are the netkeys_out tie-break (iter 23, 21 spellings + three compilers).
4. `race_records_menu` 0x1d13c, one block left (0x1d178-0x1d196): the game loads the pooled "" byte
   0x600ac into EDX, ours into EAX, so -oe tail-merges a store (GOTCHAS iter 13); diff with
   `tools/wc/fncompare.py` (instruction counts per side).
5. `pal_shift` 0x114a8: scan the LE **fixup records** (not the applied bytes) and any far-pointer
   table for 0x114a8 - dead code vs computed call - and say what the 768 bytes are (QUESTIONS 15).
6. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer; the original under the harness (LE deltas, a save state).

## BLOCKED (dead-end classes, do not re-probe)

- Stack-slot permutation of locals (`release_check` 0x34458, `wtext` 0x146d4, `enter_name`,
  `scores_screen`, `fn_1c0f4`): Watcom's scalar slot order is neither declaration nor use order.
- Two locals' register pairing - a tie-break, not a translation error (`pcx_load` ESI/EDI,
  `com_wait_byte` EBX/EDX + EAX/ECX, `com_expect` EBX/ESI + the A1 vs 8B 0D size it forces,
  `self_project` EAX/EDX in the clip sum): 24, 16 and 10 spellings move nothing.
- One local's home and constant hoisting - the same tie-break at one variable (`netkeys_out`,
  `netkeys_pack`, `netkeys_in`: c in a saved EDX vs EAX; `fn_1be3c`: the constant 0 in a pushed EDI
  vs `xor ebx,ebx`/`cmp word [g],0`); 9.5b has no explicit register variables, so it cannot be
  pinned from the declaration. SKIP at once (0 variants): these classes and a global the game keeps
  in a register across a loop (QUESTIONS 2). SKIPped: music_jingle, track_setup, classes_menu,
  fn_182d0, line_draw, net_recv, pcx_load, demo_play, view_at_curpos, wtext, release_check,
  com_wait_byte, com_expect, self_project, netkeys_out, fn_1be3c.

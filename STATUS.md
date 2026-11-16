# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries incl. the Apogee Sound System (findings/executables.md).
  Compiling the whole file with `-3` instead of `-3r` gives a byte-identical object, so the
  CPU-reorder letter is not what moves instructions - do not re-test it.
- **185 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  entries in `src/nearmiss.txt` (4 MATCH, 6 SKIP, 2 LAYOUT, 88 open). The C rebuild covers
  0x10260-0x36a4d except the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is
  the hand-written assembly module.
- Three compiler levers found while matching (GOTCHAS "This project's traps"): a constant the game
  stores through a register = an **`int` local assigned inside that arm** (msgbox 0x2a414); a
  pointer the game re-materialises three times = a **second local re-armed at the loop body's end**
  (fn_1c994 0x1c994); a `for` tail with two increments is rescheduled, so put the pointer step in the
  body (anim_tick 0x261fc). Unreachable so far: the home register of a loop-carried local (net_recv
  0x2db00, 21 spellings), a dead copy of an incoming register (fn_182d0), one moved load
  (track_setup, music_jingle), the v88bb2/v7edb0 store pair (classes_menu), the int shift into
  `err`'s own register (line_draw).
- Classes/modes (findings/classes.md): `v88bb2` = 1 AMATEUR ... 5 KID MODE (a **signed** short),
  `classes_menu` 0x19eac is that screen; `laps_menu` 0x1a148 (identical) sets `nlaps` 6/8/10.
- Car select (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b, one 6861-byte C function, `ents[0..7]`
  car icons plus **`ents[8]` as the selection box** (0x73998); `v7d338[car]` = per-car lock (0x34463).
- Race setup (findings/game-loop.md, both near misses): `fn_1be3c` builds the start grid `v70720[0..7]`
  (permutation of 0..7, `myplayer` first, `v88c6a` second in 2p, rest `rand()`-drawn); `fn_1bf00` sets
  `players[i].f64`/`.face`.
- Scores (findings/scores.md): `tbl77758` rows of 0x450 = 24 records of 0x2e (stamp +6 from
  `fn_1c994`, now byte-identical, name +0x1a), row `v88bb6 + v7ecb4`; `fn_1cad0` picks one,
  `enter_name` 0x1cb44 (21 slot diffs) and `scores_screen` 0x1ce14 (97 diffs) insert `v7ed5c` among
  the group's first four - beating none still gets the last slot.
- Record books (findings/scores.md): the drivers-by-class menu in `fn_1f858` puts its choice in
  `v88c80`; 1-3 call `race_records_menu` 0x1d13c (`fn_1d704(class, v7ecb4 + choice)`, near miss), 4
  calls `trial_records_menu` 0x1d340 (byte-identical). Menu rows are `mrows[][0x3c]` at 0x7bc00
  indexed from 1 (`mnames` = `mrows[1]`); names are `strcpy(prefix)` + `strcat` + padding.
- Menus/help (findings/menus.md): `fn_1c0f4` 0x1c0f4-0x1c88b = the F1 controls screen; control names
  in 84-byte records at 0x77432. `fn_182d0` (palette-entry copier, ~24 callers) takes 4 register args
  plus one stack arg it pops itself (`ret 4`); **its 4th argument is a word** (0x182e9
  `test cx,cx`), which our `short a4` now reproduces without touching a caller.
- Music (findings/sound.md still unwritten; the compiler fact is in GOTCHAS): `music_start` 0x1180c
  and `music_play` 0x11834 are byte-identical; the game inlines `music_start` (scan for
  `66 c7 05 cc8b08 0000`: 20 hits) and at the four inlined `music_start(song, 0)` copies keeps
  `xor edx,edx` **before** the two stores (0x11885, 0x2b6a3, 0x2cad6, 0x2e729) while 9.5b sinks it.
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of 0x3e80
  bytes A0000+`pal_b` to A0000+`pal_c`; its flippers stay `extern` (-oe loses `net_end`); page
  flips are `if`/`else` pairs of stores, never ternaries.

## Next

Take the first open item when QUEUE.md has none open; mark items CLOSED or BLOCKED.
1. Near misses: first empty-status line of `src/nearmiss.txt` (procedure in
   overnight/prompt.md step 2). Next open: `pcx_load` 0x27060 and `demo_play` 0x2e348 (5 each),
   then `com_isr` 0x106c8, `pal_shift` 0x114a8, `track_pics_load` 0x277b8, `view_at_curpos`
   0x2f408 (6 each). Read the `differs:` note first, then `tools/wc/fndiff.py` (the LEFT column is
   OURS, the right is the game's - the reverse of what overnight/prompt.md says).
2. `pcx_load` 0x27060 (5 diffs): probe shapes the way iter 17 did - copy src/ww.c whole, add the
   variants with new names, score with `tools/wc/variants.py OBJ ww 0xADDR 0xEND n1 n2 ...` where
   END cuts before the shared epilogue. Keep the probe's unchanged statements verbatim, they
   re-schedule everything otherwise.
3. Apply the iter 17 levers where their shape shows up: a constant stored through a register
   (`int` local inside the arm) in `pal_shift` 0x114a8 / `com_isr` 0x106c8; a second pointer local
   re-armed at the body end wherever the game has `push reg`/`pop reg` pairs around an inline
   strcpy/strcat (`demo_play` 0x2e348, `track_pics_load` 0x277b8).
4. Where a near miss is one moved instruction, stop early: `music_jingle` 0x11884, `track_setup`
   0x124e8, `classes_menu` 0x19eac, `fn_182d0` 0x182d0, `line_draw` 0x21f04, `net_recv` 0x2db00 are
   SKIP with the tried spellings in their notes; `demo_rec`/`pic_show` are LAYOUT. QUESTIONS 14 asks
   what makes the game keep a dead incoming-register copy - the test is a build option or front-end
   (WPP386?) that makes one appear.
5. `race_records_menu` 0x1d13c, one block left (0x1d178-0x1d196): the game loads the pooled "" byte
   0x600ac into EDX, ours into EAX, so -oe tail-merges a store (GOTCHAS iter 13). Diff with
   `tools/wc/fncompare.py` (it prints instruction counts per side, unlike fndiff).
6. Local-slot probes (QUESTIONS 9, 10): `enter_name` 21 diffs, all displacements; `scores_screen` 97
   diffs (five causes in its `differs:` note). Vary one lever per try (`short` vs `int`, etc.).
7. `fn_1c0f4` (help screen, exactly the game's size): 67 aligned diffs, five block-shape causes in
   its `differs:` note. Where a probe's low score looks like a win, first check *which register's
   value* the instruction reads (GOTCHAS iter 16 false-friend entry).
8. Inventory: archives and formats (WACKY.DAT), strings - the `42.GAM`/`20.GAM` dat records
   (`v7eca0` 1/2/3, 0x34463-0x344d4, `v88bd6` = `datfind`'s index), "BSBCS.PCX", `tbl77758`'s writer.
9. Get the original running under the harness; record the boot's segments or LE deltas and a first
   save state in `captures/`. Cheap checks: eight cars selectable off-net when registered
   (`v7e574 == 0`), and a `tbl77758` record read (what the +6 stamp holds, LIKELY).

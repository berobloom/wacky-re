# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md). Options pinned by probes.
- **189 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  entries in `src/nearmiss.txt` (8 MATCH, 10 SKIP, 2 LAYOUT, 80 open). The C rebuild covers
  0x10260-0x36a4d except the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is
  the hand-written assembly module.
- Compiler levers that worked (GOTCHAS "This project's traps"): a constant stored through a register
  = an **`int` local assigned inside that arm** (msgbox); a pointer the game re-materialises three
  times = a **second local re-armed at the loop body's end** (fn_1c994); a `for` tail with two
  increments is rescheduled, so put the pointer step in the body (anim_tick); an assignment chain
  computes in a scratch, so **write the two statements** when the game's load lands in the local's
  home (demo_play); **`volatile` pins a store 9.5b defers** (demo_play 0x2e35f); **one local used by
  two switch arms has one home register** (com_isr, com_isr_fifo: the receive arm's copy drags the
  shared local into EDX); **a shift the game does on the whole register is the operator assignment**
  (pal_shift); **a shared base load plus a copy = a temp local declared before both consumers, and
  `d = q + q` gives `add eax,eax` where `q * 2` at the use gives `lea`** (track_pics_load 0x277b8).
  Not reachable so far: a loop-carried local's home (net_recv), a dead incoming-register copy
  (fn_182d0), one moved load (track_setup, music_jingle), the v88bb2/v7edb0 pair (classes_menu), the
  int shift into `err` (line_draw), ESI vs EDI for two pushed locals (pcx_load), one copy one slot
  late (demo_play), a `short + short` sum's first operand (spilled by 9.5b, view_at_curpos).
- Probe caution (GOTCHAS iter 19): a probe's line layout and its other functions decide whether a
  shared `done:` epilogue is duplicated (328 vs 338 bytes, same source). Decide in `src/ww.c`.
- Serial link (findings/serial.md): `com_isr` 0x106c8 / `com_isr_fifo` 0x10810 byte-identical - one
  `short` byte local serves both arms of each; the FIFO variant drains while LSR bit 0 is set and
  sends <= 16 bytes/interrupt (`n` in BX). Rings `rxbuf` 0x84b00, `txbuf` 0x80afa, wrap 0x4000.
- Classes/modes (findings/classes.md): `v88bb2` = 1 AMATEUR ... 5 KID MODE (**signed** short);
  `classes_menu` 0x19eac is that screen, `laps_menu` 0x1a148 (MATCH) sets `nlaps` 6/8/10.
- Car select (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b, one 6861-byte C function,
  `ents[0..7]` car icons plus **`ents[8]` as the selection box** (0x73998); `v7d338[car]` = per-car
  lock (0x34463). Race setup (findings/game-loop.md, both near misses): `fn_1be3c` builds the start
  grid `v70720[0..7]` (`myplayer` first, `v88c6a` second in 2p, rest `rand()`-drawn).
- Scores/record books (findings/scores.md): `tbl77758` rows of 0x450 = 24 records of 0x2e (stamp +6
  from `fn_1c994`, MATCH, name +0x1a), row `v88bb6 + v7ecb4`; `fn_1cad0` picks one, `enter_name`
  0x1cb44 and `scores_screen` 0x1ce14 insert `v7ed5c` among the group's first four - beating none
  still gets the last slot. Books: `fn_1f858` -> `v88c80`, 1-3 call `race_records_menu` 0x1d13c
  (`fn_1d704`, near miss), 4 call `trial_records_menu` 0x1d340 (MATCH).
- Menus/help (findings/menus.md): `fn_1c0f4` 0x1c0f4-0x1c88b = the F1 controls screen (names in
  84-byte records at 0x77432). `fn_182d0` (palette copier) takes 4 register args plus a stack arg it
  pops (`ret 4`); **its 4th argument is a word** (0x182e9 `test cx,cx`).
- Music (findings/sound.md still unwritten): `music_start` 0x1180c / `music_play` 0x11834 MATCH; the
  game inlines `music_start` (20 hits, `66 c7 05 cc8b08 0000`) with `xor edx,edx` **before** its two
  stores where 9.5b sinks it.
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of 0x3e80
  bytes A0000+`pal_b` to A0000+`pal_c`; its flippers stay `extern` (-oe loses `net_end`); page flips
  are `if`/`else` pairs of stores, never ternaries. `pal_shift` 0x114a8 (byte-identical) scales 768
  palette bytes x4 in place and **has no caller in either code object** (QUESTIONS 15).

## Next

1. Near misses (QUEUE.md overrides these): first empty-status line of `src/nearmiss.txt` (procedure
   in overnight/prompt.md step 2). Next open: `release_check` 0x34458, `net_key` 0x2dd30,
   `com_wait_byte` 0x2df70, `com_expect` 0x2e008. Read the `differs:` note first, then
   `tools/wc/fndiff.py` - **its left column is OURS, the right one the game** (GOTCHAS iter 20); a
   `[eax + @]` vs `[eax - @]` row is the fixup mask, not a diff.
2. Use the iter-20 lever on those four: give the shared subexpression a temp local **declared
   before** the locals that consume it, and spell a doubled value as its own local (`d = q + q`).
   Probe with `tools/wc/variants.py OBJ ww 0xADDR 0xEND n1 n2 ...`, END before the shared epilogue,
   rest verbatim; compare prologues (frame size, pushes) before believing a low score.
3. `pal_shift` 0x114a8 has no caller: scan the LE **fixup records** (not the applied bytes) and any
   far-pointer table for 0x114a8 to settle dead-code vs computed call, and say what the 768 bytes
   are (QUESTIONS 15).
4. Stop early where a near miss is one moved instruction: `music_jingle` 0x11884, `track_setup`
   0x124e8, `classes_menu` 0x19eac, `fn_182d0` 0x182d0, `line_draw` 0x21f04, `net_recv` 0x2db00,
   `pcx_load` 0x27060, `demo_play` 0x2e348, `view_at_curpos` 0x2f408, `wtext` 0x146d4 (slots only)
   are SKIP - one probe compile is enough to call a tie-break (GOTCHAS "coin flip").
5. `race_records_menu` 0x1d13c, one block left (0x1d178-0x1d196): the game loads the pooled "" byte
   0x600ac into EDX, ours into EAX, so -oe tail-merges a store (GOTCHAS iter 13). Diff with
   `tools/wc/fncompare.py` (instruction counts per side, unlike fndiff).
6. Local-slot probes (QUESTIONS 9, 10): `enter_name` 21 diffs, all displacements; `scores_screen` 97;
   `fn_1c0f4` 67 aligned. Vary one lever per try, and check *which register's value* an instruction
   reads before believing a probe's low score (iter 16).
7. Inventory: archives and formats (WACKY.DAT), strings - the `42.GAM`/`20.GAM` dat records
   (`v7eca0` 1/2/3, 0x34463-0x344d4, `v88bd6` = `datfind`'s index), "BSBCS.PCX", `tbl77758`'s writer;
   and the original under the harness (boot segments/LE deltas, one save state in `captures/`).

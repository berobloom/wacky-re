# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **190 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  entries in `src/nearmiss.txt` (9 MATCH, 12 SKIP, 2 LAYOUT, 77 open). The C rebuild covers
  0x10260-0x36a4d except the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is
  the hand-written assembly module.
- Compiler levers that worked (GOTCHAS "This project's traps" lists them with their evidence): an
  `int` local assigned inside one arm, a second local re-armed at the loop body's end, the pointer
  step of a `for` tail written in the body, `volatile` to pin a store 9.5b defers, one local for two
  switch arms, the operator assignment for a whole-register shift; a temp local declared **before**
  both consumers of a shared base, and `d = q + q` for `add eax,eax` where `q * 2` gives `lea`
  (track_pics_load); a second, word-typed C name for a global the game truncates through a *word*
  load (`#pragma aux` renames data) plus `& 0xff`; and an assignment the call does not read belongs
  **below** that call (net_key 0x2dd30, baseline 190).
- Not reachable so far: a loop-carried local's home (net_recv), a dead incoming-register copy
  (fn_182d0), one moved load (track_setup, music_jingle), the v88bb2/v7edb0 pair (classes_menu), the
  int shift into `err` (line_draw), ESI vs EDI (pcx_load), one copy late (demo_play), a `short +
  short` sum's first operand (view_at_curpos), `com_wait_byte`'s three register homes.
- Probe caution (GOTCHAS iter 19, 21): a probe's line layout decides whether a shared `done:`
  epilogue is duplicated (328 vs 338 bytes, same source), and a probe missing a game function that
  `src/ww.c` defines earlier compiles a *different* function (control 24 diffs vs the file's 9).
- Serial link (findings/serial.md): `com_isr` 0x106c8 / `com_isr_fifo` 0x10810 byte-identical, one
  `short` byte local in both arms, FIFO variant drains on LSR bit 0 and sends <= 16 bytes per
  interrupt; rings `rxbuf` 0x84b00, `txbuf` 0x80afa, wrap 0x4000. `net_key` 0x2dd30 (MATCH iter 21)
  keeps `v88cae` = the **low byte** of the sequence byte `v7ed8c` (0x2dd42-0x2dd4e).
- Classes/modes (findings/classes.md): `v88bb2` = 1 AMATEUR ... 5 KID MODE (**signed** short),
  `laps_menu` 0x1a148 (MATCH) sets `nlaps` 6/8/10. Release check 0x34463-0x344d4: `42.GAM` ->
  `v7eca0` 3, `20.GAM` -> 2, neither -> shareware (`v7e574` 1).
- Car select (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b, one 6861-byte C function, with
  **`ents[8]` (0x73998) as the selection box** and `v7d338[car]` the per-car lock (0x34463).
- Scores/records (findings/scores.md): `tbl77758` rows of 0x450 = 24 records of 0x2e (stamp +6 from
  `fn_1c994` MATCH, name +0x1a), row `v88bb6 + v7ecb4`; `enter_name` 0x1cb44 / `scores_screen`
  0x1ce14 insert `v7ed5c` among the group's first four; books 1-3 `race_records_menu` (near miss).
- Menus/help (findings/menus.md): `fn_1c0f4` = the F1 controls screen (names in 84-byte records at
  0x77432). `fn_182d0` (palette copier) takes 4 register args plus a stack arg (`ret 4`) and **its
  4th argument is a word** (0x182e9 `test cx,cx`).
- Music (findings/sound.md still unwritten): `music_start` 0x1180c / `music_play` 0x11834 MATCH; the
  game inlines `music_start` with `xor edx,edx` **before** its two stores where 9.5b sinks it.
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of 0x3e80
  bytes A0000+`pal_b` to A0000+`pal_c`; page flips are `if`/`else` pairs of stores, never ternaries.
  `pal_shift` 0x114a8 (byte-identical) scales 768 palette bytes x4 in place and **has no caller in
  either code object** (QUESTIONS 15).

## Next

1. Near misses (QUEUE.md overrides these): first empty-status line of `src/nearmiss.txt` (procedure
   in overnight/prompt.md step 2). Next open: `com_expect` 0x2e008, `fn_16ca0` 0x16ca0,
   `self_project` 0x26a3c, `netkeys_out` 0x2d9e4. Read the `differs:` note first, then
   `tools/wc/fndiff.py` - **its left column is OURS, the right the game** (GOTCHAS iter 20); a
   `[eax + @]` vs `[eax - @]` row is the fixup mask, and a tail `lea eax,[eax]`/`nop` row is the
   size gap of the row above it (iter 21), not a diff.
2. `com_expect` 0x2e008 (9 diffs) is `com_wait_byte`'s shape: want/t0 swap EBX/ESI and "all 30
   orders of t0, c, r tried" - read the note, at most one probe compile, then SKIP.
3. Apply the iter-21 levers to the open list: a second word-typed name for a global the game
   truncates through, and sink an assignment below a call that does not read it. Probe with
   `variants.py OBJ ww 0xADDR 0xEND n1 n2 ...` only if the control scores what fndiff scores.
4. `pal_shift` 0x114a8: scan the LE **fixup records** (not the applied bytes) and any far-pointer
   table for 0x114a8 - dead code vs computed call - and say what the 768 bytes are (QUESTIONS 15).
5. `race_records_menu` 0x1d13c, one block left (0x1d178-0x1d196): the game loads the pooled "" byte
   0x600ac into EDX, ours into EAX, so -oe tail-merges a store (GOTCHAS iter 13). Diff with
   `tools/wc/fncompare.py`, which counts instructions per side.
6. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer; the original under the harness (LE deltas, a save
   state in `captures/`).

## BLOCKED (dead-end classes, do not re-probe)

- Stack-slot permutation of locals (`release_check` 0x34458, `wtext` 0x146d4, `enter_name`,
  `scores_screen`, `fn_1c0f4`): Watcom's scalar slot order is neither declaration nor use order.
- Two locals' register pairing (`pcx_load` ESI/EDI, `com_wait_byte` EBX/EDX + EAX/ECX temp): 24 and
  16 spellings move nothing - a tie-break, not a translation error.
- SKIP at once (0 variants): those two classes and a global the game keeps in a register across a
  loop (QUESTIONS 2). SKIPped so far: music_jingle, track_setup, classes_menu, fn_182d0, line_draw,
  net_recv, pcx_load, demo_play, view_at_curpos, wtext, release_check, com_wait_byte.

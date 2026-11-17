# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **191 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  `src/nearmiss.txt` entries (10 MATCH, 14 SKIP, 2 LAYOUT, 74 open); C covers 0x10260-0x36a4d bar
  the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is the hand-written asm.
- Compiler levers that worked (GOTCHAS "This project's traps" lists them with their evidence): an
  `int` local assigned inside one arm, a second local re-armed at the loop body's end, the pointer
  step of a `for` tail written in the body, `volatile` to pin a store 9.5b defers, one local for two
  switch arms, the operator assignment for a whole-register shift; a temp declared **before** both
  consumers of a shared base, and `d = q + q` for `add eax,eax` where `q * 2` gives `lea`; a second,
  word-typed C name for a global the game truncates through a *word* load (`#pragma aux` renames
  data) plus `& 0xff`; an assignment the call does not read belongs **below** that call (net_key);
  and the load of the *next* statement **above** the assignment it splits (fn_16ca0 0x16ca0).
- Not reachable so far: a loop-carried local's home (net_recv), a dead incoming-register copy
  (fn_182d0), one moved load (track_setup, music_jingle), the v88bb2/v7edb0 pair (classes_menu), the
  int shift into `err` (line_draw), ESI vs EDI (pcx_load), one copy late (demo_play), a `short +
  short` sum's first operand (view_at_curpos), `com_wait_byte`/`com_expect`'s three register homes,
  and `self_project`'s 3 real rows (0x26df2: the game's clip sum starts from spr_y).
- Probe caution (GOTCHAS iter 19, 21, 22): a probe's line layout decides whether a shared `done:`
  epilogue is duplicated (328 vs 338 bytes, same source); a probe that only *prototypes* a game
  function `src/ww.c` defines earlier compiles a different function - define it there instead.
- `tools/wc/fndiff.py` counts fake rows: a `[reg +/- @]` pair whose displacement is a fixup is the
  0x90 mask filler (`com_getc_wait` is a MATCH and still shows one) - 7 of `self_project`'s 10 rows.
  `src/check.sh` (the byte compare) is the verdict.
- Serial link (findings/serial.md): `com_isr` 0x106c8 / `com_isr_fifo` 0x10810 byte-identical, one
  `short` byte local in both arms, FIFO drains on LSR bit 0, sends <= 16 bytes per interrupt; rings
  `rxbuf` 0x84b00, `txbuf` 0x80afa, wrap 0x4000. `net_key` 0x2dd30 keeps `v88cae` = the **low byte**
  of the sequence byte `v7ed8c` (0x2dd42-0x2dd4e); `net_sync` 0x2e0fc identical => `com_expect(want,
  timeout)` takes want in EAX (iter 22).
- Classes/modes (findings/classes.md): `v88bb2` = 1 AMATEUR ... 5 KID MODE (**signed** short),
  `laps_menu` sets `nlaps` 6/8/10; release check 0x34463-0x344d4: `42.GAM` -> `v7eca0` 3, `20.GAM`
  -> 2, neither -> shareware (`v7e574` 1).
- Car select (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b, one 6861-byte C function, with
  **`ents[8]` (0x73998) as the selection box** and `v7d338[car]` the per-car lock (0x34463).
- Scores/records (findings/scores.md): `tbl77758` rows of 0x450 = 24 records of 0x2e (stamp +6,
  name +0x1a), row `v88bb6 + v7ecb4`; books 1-3 `race_records_menu` (near miss).
- Menus/help (findings/menus.md): `fn_1c0f4` = the F1 controls screen (names in 84-byte records at
  0x77432); `fn_182d0` (palette copier) takes 4 register args + a stack arg (`ret 4`), **its 4th
  argument is a word** (0x182e9 `test cx,cx`).
- Music (findings/sound.md still unwritten): `music_start` / `music_play` MATCH; the game inlines
  `music_start` with `xor edx,edx` **before** its two stores where 9.5b sinks it.
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of 0x3e80
  bytes A0000+`pal_b` to A0000+`pal_c`; page flips are `if`/`else` pairs of stores, never ternaries.
  `pal_shift` 0x114a8 (identical) scales 768 palette bytes x4 in place, **no caller** (QUESTIONS 15).

## Next

1. Near misses (QUEUE.md overrides these): first empty-status line of `src/nearmiss.txt` (procedure
   in overnight/prompt.md step 2). Next open: `netkeys_out` 0x2d9e4, `shot_step` 0x25e6c,
   `fn_1be3c` 0x1be3c, `race_records_menu` 0x1d13c. Read the `differs:` note, then `fndiff.py` -
   **left column OURS, right the game** (GOTCHAS iter 20) - and subtract the fake rows (fixup-mask
   signs, tail padding); one probe compile is the limit when the only rows are register homes.
2. Apply the iter-22 lever: where the game's listing has a load **between** the two halves of an
   expression (load, store, shift, load, store), write that load earlier in the C. `shot_step` and
   `netkeys_out` have no `differs:` note yet - diff them first.
3. `pal_shift` 0x114a8: scan the LE **fixup records** (not the applied bytes) and any far-pointer
   table for 0x114a8 - dead code vs computed call - and say what the 768 bytes are (QUESTIONS 15).
4. `race_records_menu` 0x1d13c, one block left (0x1d178-0x1d196): the game loads the pooled "" byte
   0x600ac into EDX, ours into EAX, so -oe tail-merges a store (GOTCHAS iter 13); diff with
   `tools/wc/fncompare.py` (instruction counts per side).
5. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer; the original under the harness (LE deltas, a save state).

## BLOCKED (dead-end classes, do not re-probe)

- Stack-slot permutation of locals (`release_check` 0x34458, `wtext` 0x146d4, `enter_name`,
  `scores_screen`, `fn_1c0f4`): Watcom's scalar slot order is neither declaration nor use order.
- Two locals' register pairing - a tie-break, not a translation error (`pcx_load` ESI/EDI,
  `com_wait_byte` EBX/EDX + EAX/ECX, `com_expect` EBX/ESI + the A1 vs 8B 0D size it forces,
  `self_project` EAX/EDX in the clip sum): 24, 16 and 10 spellings move nothing.
- SKIP at once (0 variants): those two classes and a global the game keeps in a register across a
  loop (QUESTIONS 2). SKIPped: music_jingle, track_setup, classes_menu, fn_182d0, line_draw,
  net_recv, pcx_load, demo_play, view_at_curpos, wtext, release_check, com_wait_byte, com_expect,
  self_project.

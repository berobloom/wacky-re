# Car select: `cars_menu` 0x1a380 (CHOOSE VEHICLE)

Question: what does the car-select screen do, and what are its locals and globals?
Status: **OPEN** — the whole function is mapped and `src/ww.c` now contains the
rebuild (`cars_menu`, near miss: 6562 vs 6861 bytes; 71 % of the instruction rows
agree once the stack-slot permutation and the fixup bytes are masked, see
"Rebuild status" below). The local roles are now read off matching code, but the
scalar slot numbers are the open part (QUESTIONS.md 5).

## The function (CONFIRMED, disassembly 0x1a380-0x1be3b)

- 6861 bytes, one C function: one prologue (`push ebx,ecx,edx,esi,edi,ebp;
  sub esp,0x17c` 0x1a380-0x1a386) and one epilogue (0x1be2f-0x1be3b). Three
  early returns: 0x1b198 and 0x1b37b (both on the net `EXCESSIVE ERRORS` path)
  and 0x1b540 (`v88b9e = 1`, set at 0x1b537 when `v88cec` is set).
  Largest function decoded so far.
- The 0x20 bytes at 0x1a360 are **not padding** (STATUS said padding): two
  4-entry jump tables. 0x1a360 = 0x1b7d7, 0x1b828, 0x1b86e, 0x1b8c1, used by
  `jmp dword cs:[esi*4+0x1a360]` 0x1b7cf after `cmp si,3 / ja 0x1b8fd` 0x1b7c6
  (switch on the per-car state, default = skip). 0x1a370 = 0x1bbc3, 0x1bbf9,
  0x1bc35, 0x1bc55, used by `jmp dword cs:[edx*4+0x1a370]` 0x1bbbb on
  `state-3` after `cmp ax,3 / ja 0x1be13` 0x1bbaa.
- The outer frame loop is `0x1b9b5` (the animation stage) and `0x1aae0` (the
  input stage); `cmp word [esp+0x164],5 / jne 0x1b9b5` 0x1be13 ends it.

## Stack frame (offsets CONFIRMED as printed; roles are labelled)

NB the printed offset is relative to `esp` **at that instruction**: the five-argument
calls (`ents_show`, `showlines`) push one dword, so a read that follows a `push 0` is
4 below the row it appears to match (0x1adf7 reads 0x138, 0x1ba75 reads 0x15c - CONFIRMED
by the rebuild, where the same C reads land in the same slots).

0x00, 0x50, 0xa0 `char[0x50]` × 3, then 0xf0, 0x100 `short[8]` × 2, then 27
dword locals 0x110-0x178: 0xf0 + 0x20 + 0x6c = 0x17c = the `sub esp` size
(CONFIRMED arithmetic, so the port needs exactly this set).

| esp+ | value / use | address | level |
|---|---|---|---|
| 0x00 | buf0: this function only stores the pooled empty string byte there; passed to `print_at` (`mov ebx,esp`) and to `fn_1c0f4` as arg1 | 0x1a3ce, 0x1a5df, 0x1b2df, 0x1b415 | CONFIRMED stores, LIKELY use |
| 0x50 | buf1: `"CHOOSE VEHICLE"`, replaced by `"PLAYER TWO"` / `"PLAYER ONE"` | 0x1a3c9, 0x1a40e, 0x1a424 | CONFIRMED |
| 0xa0 | buf2: `ctl_label`'s hint line (arg2), printed at (0x2e, 0xbc) | 0x1a437, 0x1a605 | CONFIRMED |
| 0xf0 | short[8], the eight icon x values (cars 0-3, then 4-7) | 0x1a4f4, 0x1a567 | CONFIRMED store, no read here |
| 0x100 | short[8], per-car cursor state 0..3 (the adjacency/animation table of the switch at 0x1b7cf) | read 0x1a6ad, 0x1b7ba; written 0x1a556, 0x1b6e1-0x1b8f6 | CONFIRMED set |
| 0x110 | stage-counter of the "choice locked in" animation | 0x1b993, 0x1bbb3, 0x1bc3c | LIKELY |
| 0x114 | = 0x2e, left margin: print_at x of the two info lines | 0x1a38c, 0x1a5cd | CONFIRMED |
| 0x118 | idle timer; when > 4 it advances 0x160 | 0x1afb9, 0x1afe0 | LIKELY |
| 0x11c | `page_draw` as it was on entry, restored at 0x1a61e | 0x1a3d8, 0x1a3e5, 0x1a615 | CONFIRMED |
| 0x120, 0x144 | "the other row moved first" flags (up / down) | 0x1b59f, 0x1b5a8, 0x1b5b5, 0x1b5c0, 0x1b629, 0x1b634, 0x1b63f, 0x1b64a | LIKELY |
| 0x124 | = 0x60, caption y | 0x1a445, 0x1a5bc | CONFIRMED |
| 0x128 | countdown 9..0 for the name-line loop | 0x1a84d, 0x1a934, 0x1a946 | CONFIRMED |
| 0x12c | = 0x3a, row 0 y (compared with `ents[i].y`) | 0x1a478, 0x1af30, 0x1b8c8 | CONFIRMED |
| 0x130, 0x14c | 8-step slide countdown, up row / down row | 0x1b5ec, 0x1b676, 0x1b917, 0x1b92d | LIKELY |
| 0x134, 0x138, 0x13c | `[0x154] + 4`, the y of the current name row | 0x1a858, 0x1aad9, 0x1b9ae | LIKELY |
| 0x140 | `find12`'s result = the car id under the cursor (-1 = none); becomes `myplayer` on exit | 0x1a880, 0x1ae62, 0x1b461, 0x1bb3c, 0x1be29 | CONFIRMED |
| 0x148 | selects the 60-tick or 16-tick wait | 0x1a83d, 0x1a9ec, 0x1aa04, 0x1aa23 | CONFIRMED test |
| 0x150 | cursor move state: 1 up, 2 down, 4 returning, 0 idle | set 0x1b5aa, 0x1b634, 0x1af97, 0x1aef4; read 0x1af08, 0x1b557, 0x1b5f7 | CONFIRMED values |
| 0x154, 0x168 | selection-box sprite y / x (set to 0x8e / 0xec) | 0x1a5e5, 0x1a605, 0x1ae0f, 0x1ae24 | CONFIRMED |
| 0x158 | added to 0x178 when picking the box sprite in the confirm stage | 0x1b9fe, 0x1ba0b | LIKELY |
| 0x15c | stage-4 countdown (-1, then 0..10); also `ents_show`'s `sel` in stage 1-2 (0x1ba86) | 0x1b9a3, 0x1ba75, 0x1bbf9, 0x1bc1a | LIKELY |
| 0x160 | 0/1 toggle: which frame of the selection-box pair | 0x1afe0, 0x1aff2, 0x1ae2a | LIKELY |
| 0x164 | stage machine 1 (set 0x1b97c), 2 (0x1bbde), 3 (0x1bc25), 4 (0x1bc45), 5 (0x1be08); loop while != 5 | 0x1be13 | CONFIRMED values |
| 0x16c | = 0x2e, column 0 x (compared with `ents[i].x`) | 0x1a46d, 0x1b6d7, 0x1b765 | CONFIRMED |
| 0x170, 0x174 | the cursor's y / x in the icon grid (find12's args; start 0x72 / 0xee) | 0x1a513, 0x1a574, 0x1a864, 0x1a871 | CONFIRMED args |
| 0x178 | 0 = PLAYER ONE, 2 = PLAYER TWO (the `v88b7e == 1` case); base index into `sp6a878` | 0x1a3b9, 0x1a413, 0x1a6eb, 0x1ae37 | CONFIRMED |

## The eight car icons live in `ents[]` (CONFIRMED)

- Setup loop 1 0x1a4b1-0x1a4ff: `ents[0..3]`: `id` = 0..3, `x` = 0x2e + 0x40*i
  (`spr_x` walks it), `y` = 0x3a, `f8` = `frame` = 0.
  Setup loop 2 0x1a52d-0x1a58d: `ents[4..7]`: `id` = 4..7, `y` = 0x72, and
  these four get `f2` = 0 as well.
  The writes are `[eax+0x7392c+0x10/0x14/0x16/0x0c/0x08]` with `eax` = 12*i,
  i.e. `ents[i]` + 4/8/10/0/6 — the `struct ent` fields, base 0x73938.
- The icon's picture is `faces[ents[i].id].img[ents[i].frame]` (0x1a699,
  0x1bacd); the eight are drawn at 0x1a665-0x1a6d0 with `spr_w` 0x1c,
  `spr_h` 0x26, and `sprptr += 0x10a0` when the per-car state at `[esp+0x100]`
  is not 1 (0x1a6ad-0x1a6b8) — LIKELY a dim/alternate variant of the same
  sprite, 10 frames past it.
- The cursor is a grid point: `find12(x = [esp+0x174], y = [esp+0x170])`
  returns the `id` of the `ents[]` record at that position (0x1a879; find12
  0x16a7c scans the 8 records by x/y).
- The two rows slide when the cursor moves: `ents[i].x` / `ents[i].y` get
  `add`/`sub` of a step in the state machine 0x1b7d7-0x1b8fd
  (0x1b81c, 0x1b862, 0x1b8b8, 0x1b8f6); `ents[i].f2` latches `frame == 2`
  and `frame == 6` at 0x1b946-0x1b96c.
- Each arm of that switch (table 0x1a360, dispatch `cmp si,3 / ja 0x1b8fd /
  jmp [esi*4+0x1a360]` 0x1b7c2-0x1b7cf) also sets the icon's picture frame at
  its arm head: state 0 stores `ents[i].frame = 2` (0x1b7de) and state 1 stores
  `= 6` (0x1b82f), both unconditional; states 2 and 3 store 2 or 6 only in the
  branch that re-tags the record (0x1b89a, 0x1b8ed and the shared 0x1b8a3 block
  both arms jump to - `jne 0x1b8a3` 0x1b8e1). The 2 and the 6 are the
  registers CX/SI set up before the loop (0x1b7b5 `mov ecx,2`, 0x1b7b0
  `mov edi,8` = the step) and CX is reused for the arm's `carstat = 2` store
  (0x1b80f), so the value is not an immediate at the store. CONFIRMED by
  `tools/wc/fixsites.py --arms cars_menu` (the arm's only entry is its table
  slot: no `jmp` to 0x1b7d7 exists in the function), and by the f2 latch above
  only ever seeing 2 and 6. Our rebuild had lost the state-0 store entirely
  (iter 67): a row that slides left kept its old picture.

## There are nine `struct ent` records here, not eight (CONFIRMED)

- Both setup loops walk 0..7 (0x1a58a `cmp edx,7`), but two other walks run 0..8:
  0x1a792 (`lea eax,[ecx*8]; add eax,eax; lea eax,[eax+eax*4]` -> 0xc*i,
  `cmp eax,0x6c` = 8*8) and 0x1b96f (`lea eax,[edx*8]; lea esi,[eax+eax*4]`,
  `cmp esi,0x60`) reach past the eight icon records, i.e. they scan/clear up to
  the record at 0x73938 + 8*0xc = **0x73998**.
- That 9th record is the selection box: 0x1a58f-0x1a5b2 (just after the two
  8-record setup loops) writes `ents[8].x` = 0xec, `ents[8].y` = 0x8e,
  `ents[8].f8` = 0 and `ents[8].frame` = 0; the draw at 0x1a6d2-0x1a710 takes
  spr_x/spr_y from `ents[8].x/y` with spr_w 0x1c / spr_h 0x26 and
  `sprptr = sp6a878[pbase]` (0x1a6fb), and the second one at 0x1ae03-0x1ae37 uses
  the box *locals* ([esp+0x168]/[esp+0x154]) with `sp6a878[pbase + flip]`. So
  `ents[8].frame` is set to 0 here and the box's two-frame toggle lives in the
  0x160 local, not in the record.
- Consequence: what the first pass of this file called `carbox_x` (0x7399c) /
  `carbox_y` (0x7399e) are `ents[8].x` / `ents[8].y` (0x73938 + 8*0xc + 4/6), and
  `ents` has to be declared `struct ent ents[9]` for those writes to land. The
  rebuild does, and its writes match.

## `v7d338[]` = the per-car lock (STRONGLY SUPPORTED; closes most of QUESTIONS 4)

Eight ints, indexed by car id (== ent slot here, since `id == i` at setup).

- `ents_show` draws a 9 x 0x36 sprite (`lp7d804`) at (`spr_x`-0xb,
  `spr_y`+0x12) over every ent whose slot flag is set and `sel == -1`
  (0x1462d-0x14659) — a lock mark over the icon.
- `cars_menu` 0x1ac0e-0x1ac3f (net path) and 0x1b461-0x1b491: if
  `v7d338[car under the cursor] != 0` then `joy88b40 = 0` and the two cursor
  counters (0x160, 0x118) are zeroed, so the choice cannot be confirmed.
- On confirm with the car locked: `voice_play(0x1a)` and three lines into
  `str7c854` slots 0..2 then `showlines(3,1,0,0)` (0x1b493-0x1b4dc):
  `" PLEASE READ ORDERING INFO "`, `" TO FIND OUT HOW TO RACE   "`,
  `" WITH THIS ANIMAL.         "` — the order-the-full-game nag.
- The only writer is `fn_1a2f8` 0x1a2f8 (clears all 8, then sets [0], [1],
  [5], [6]): off-net when `v7e574`, on-net when `v7ec9c == 1`.
- `v7e574` is the shareware flag: 0x34463-0x344d4 looks up `"42.GAM"` then
  `"20.GAM"` with `datfind`, and sets `v7e574 = 1`, `v7eca0 = 1` when neither
  is present (2 when `20.GAM`, 3 when `42.GAM`); at 0x344e7 it prints
  `"SHAREWARE RELEASE."` in that case.
  So: the shareware release locks cars 0, 1, 5 and 6. CONFIRMED for the code,
  LIKELY for "shareware builds hide four cars".

## Per-frame drawing (CONFIRMED addresses)

- Caption is centred: `x = (0x140 - 8*strlen(buf1)) / 2` 0x1a43c-0x1a46b, kept
  in ECX (0x1a4a9) across both setup loops, then `movsx eax, cx` 0x1a5c8 as
  print_at's arg1.
- The selection box (`sp6a878[id0 + id1]`, `spr_w = 0x1c`, `spr_h = 0x26`) is drawn in
  three places: 0x1a983-0x1a996, 0x1ae3f-0x1ae5d (right after the `ents_show` 0x1adfe)
  and 0x1ba13-0x1ba31. CONFIRMED by the three `call spr_draw`; our C had only two of
  them until iter 60 - the middle one was missing, and fndiff hid it.
- Info lines: buf0 at (0x2e, 0xaf) 0x1a5da, buf2 at (0x2e, 0xbc) 0x1a605.
- `page_draw` is set to 0x7d00 at 0x1a5b3, restored from 0x11c at 0x1a61e, then
  blinked 0 / 0x3e80 once per frame (0x1a744, 0x1a9ce, 0x1aec3, 0x1bb0c), with
  `page_copy` (0x36f14) and a `ticks - v7ea70` wait (8 ticks 0x1a76f, 16 /
  60 ticks 0x1aa04/0x1aa23, 0x1a ticks 0x1aeef, 10 ticks 0x1af1b/0x1bb37,
  4 ticks 0x1afb4).
- Each frame ends with `page_show` (0x37de0, LIKELY the flip/present) and the
  `key88ccd` screenshot hook (0x1a724, 0x1a9ae, 0x1ae99, 0x1baf5).
- The car-name block 0x1a8a7-0x1a932 (CONFIRMED; our C was wrong until iter 75):
  `p7d838 = tab6e5b0[cd]` 0x1a8d1 and `p7d764 = p7d838 + 1` (stored twice, 0x1a8d6
  and 0x1a8ee - the first is dead), then `add dword [0x7d838],4` 0x1a8f6: the
  stream pointer moves **once per pass**, not once per name row. `pname_desc` is
  called once at 0x1a90d and the row loop 0x1a912-0x1a932 (`add esi,0x3c; add
  edi,0xc; cmp esi,ebp; jne`) touches only the row string and y, calling `wtext`
  twice. Our C had `p7d838 += 2` and `pname_desc` inside that loop: difffn
  (`TICK=8 SEED=0x88cce=1:b`, findings/menus.md) showed 0x7d838 ending 0x08 where
  the game leaves 0x04. p7d838 is the `.SP` stream cursor the scaled blit
  `spr_draw_scaled` steps (findings/formats.md), so what the block leaves behind is what
  the next draw reads.
- `ents_draw(9)` at 0x1a71a; `voice_play(0x12)` at 0x1a632 (LIKELY the
  "choose a car" voice), `voice_play(0x1a)` at 0x1b4a7 (with the ORDERING INFO
  lines) and `voice_play([esp+0x140])` at 0x1b987 - the car id, so LIKELY the
  chosen car's voice.
- The input stage (0x1aa80-0x1b52e) is entered only when
  `ents[4].frame == 6` (0x1aa28), and it starts with `kbd_off()` (0x1aa7b).
  Otherwise 0x1aa32-0x1aa79 increments `ents[4..7].frame`
  (0x73972/0x7397e/0x7398a/0x73996) and bumps `ents[0..3].frame`
  (`word [eax+0x73942]`, eax = 0..0x24, reset to 0 above 7), then jumps back.

## Control and net unpack (CONFIRMED pairs of `and`/store)

- `joy88b46 = (key88cd0 == 0x3b)` 0x1ab47-0x1ab55 — LIKELY the F1 help key: it
  leads to `fn_1c0f4(buf0)` and `msgbox("REMOTE IS VIEWING HELP")`
  (0x60b8c, 0x1b0a7) and to a full control reset 0x1b0b1-0x1b129.
- `v88cef` (the remote control byte) unpacks at 0x1ac8c-0x1ad1d:
  bit0 -> `key88cd5`, bit1 -> `key88cdd` (only when this instance's player
  index matches the net slot: `(v88b7e == 1 && netgame == 1) ||
  (v88b7e != 1 && netgame == 2)` - CONFIRMED, 0x1acb7 `cmp v88b7e,1`, 0x1acbf
  `jne 0x1accd`, 0x1acc1 `cmp netgame,1 / jne 0x1ad23`, 0x1accd `cmp netgame,2
  / je 0x1acd7`; the earlier note had the two player values swapped),
  bit2 -> `joy88b40`,
  bit3 -> `joy88b42`, bit4 -> `joy88b44`, bit5 -> `joy88b46`,
  bit6 -> `v88cec`; otherwise bit5 only, and only if `joy88b46 == 0` (0x1ad23).
- `joy88b42 != 0 || v88cce != 0` forces `joy88b40 = 1` (0x1ab9a, 0x1b3a0,
  0x1b3b3) — LIKELY: `joy88b40` is this screen's confirm/fire control.
- Per-player control bytes: `v7b6d9` / `v7b6db` in 12-byte records set
  `key88cd5` / `key88cdd` (0x1abc2, 0x1abd5), and `cfg7712c == 0` picks
  `joy88b44` vs `[v88b7e*12+0x7b6df]` for `joy88b40` (0x1b2c4-0x1b39a).
- `net_key` returns an error count in AX into `net88c14`; nonzero -> `fn_1c068`
  + `msgbox("EXCESSIVE ERRORS")` (0x60b78) + `ticks = ticks2 = 0` + wait until
  `ticks > 0x12c` + `fn_1c0ac` + return (0x1ac54-0x1ad8a, 0x1b146-0x1b1a4,
  0x1b32b-0x1b387).
- `v7ecc4` counts the input-loop passes and clamps `v7ed20`/`v7ed1c` from
  0x550/0x2a8 to 0x2c/0x220 (0x1ac59-0x1ac8c): already in findings/menus.md
  lines 83-84, and the help path saves/forces 0xaa0 (0x1b130-0x1b266).

## The tail: what runs after a car is chosen (0x1bc55-0x1be29, CONFIRMED)

- `v88b9e = 0`; `ents_init()` (0x1bc5e).
- If `twoplayer && v88b7e == 1`: four rows `" PLAY FOR 5  HITS "` ..
  `" PLAY FOR 20 HITS "` (0x60bf8/0x60c0c/0x60c20/0x60c34, note the double
  space in the first) into `mnames[0..3]` (0x7bc3c + 0x3c*i), `v88bb0 = 0xd`,
  `v88c80 = 1`, `menu(4,0,0,0)` -> `v7eda0 = 5 * choice` (0x1bcfb, 0x1bd03;
  compared with a counter at 0x2bc9a and 0x31444) = the deathmatch hit target.
- `netgame && net88c14` -> jump straight to the return (0x1bd1a); `v7ecc0 != 0`
  -> `v88b9e = 1` (0x1bd29).
- Then the chain findings/classes.md already describes, each gated on
  `v88b9e == 0`: `classes_menu` 0x1bd3c, `laps_menu` 0x1bd4b,
  `engines_menu(2)` 0x1bd5f, `zones_menu(2, arg2)` 0x1bd87 where
  `arg2 = twoplayer ? twoplayer : netvoices` (0x1bd6e-0x1bd7f - `netvoices` is
  loaded as a *word* here, 0x7ec74).
- If that whole chain is skipped (`v88b9e != 0`, 0x1bd8c): with
  `ok = (!twoplayer && !netvoices) && (!splitscreen || v88b7e == 1)`
  (0x1bd9a-0x1bdca) `v7d854 = v7d85c` and `fn_1832c()` runs (0x1bdcf-0x1bdd9);
  else when `twoplayer || netvoices`: `v88bb6 = 3`, `v88b88 = v88c96`
  (0x1bde0-0x1be02).
- Exit: `myplayer = [esp+0x140]` (0x1be29) — the car the player picked.

## Rebuild status (iter 7)

`src/ww.c` has `cars_menu`: 6562 bytes vs the game's 6861, differing from +8
(the frame `sub esp,0x17c` is right; the slot numbers are not). Masking the esp
displacements and the relocation-filled addresses, 876 of the 1229 disassembly
rows (71 %) already sit in the same order with the same opcodes and immediates,
including both switch jump tables landing in the 0x1a360/0x1a370 gap.

The rest is one permutation plus a few statement orders: the game's 27 scalar
slots (0x110..0x178) and ours (0x110..0x174) are the same variables in a
different order - ours 0x16c is the game's 0x114 (the 0x2e box x), ours 0x160 is
the game's 0x140 (the car), ours 0x140 is the game's 0x110 (the loop counter),
ours 0x174 is the game's 0x150 (the slide step). Arrays are allocated in
declaration order (the three 0x50 buffers and the two `short[8]`s land right,
and reversing the buffer declarations permutes them), but Watcom gives the
scalars their slots in an order set by its register allocation, not by
declaration: our 26 spilled scalars are a fixed permutation of the game's 27,
and the game additionally spills what we keep live in a register. Open in
QUESTIONS.md.

Behaviour is closed on this screen (iter 85): `tools/wc/difffn.py cars_menu` with
`TICK=8 SEED=0x88cce=1:b,0x88cd0=1:b,0x88c46=4:w RESEED=0x88cd0=0:b CALLS=20000
COUNT=120000000` returns on both sides and leaves the data segment identical
(control SAME, hotfn 18136 insns at 819 addrs) - see findings/menus.md for the
run and `src/nearmiss.txt` for the verdict line, so nothing above this frame
permutation is a behavioural difference.

## Traps

- A loop that re-bumps a global stream pointer where the game bumps it once per
  pass is invisible to the byte comparison (cars_menu 0x1a380: 416-row near miss,
  `p7d838` left 2 shorts past the game's - iter 75 found it only by running the
  function). Compare `p7d838` at the end, not the row count.
- 0x1a360/0x1a370 are jump tables for the two switches, not padding: check
  `jmp dword ptr cs:[reg*4+disp]` before calling a gap dead space.
- `ents[]` is the car-icon table here (8 records, `id == slot`), so reads of
  `ents[]` outside the race do not imply race entity state.
- The two setup loops address the same two arrays with different index
  expressions: `[esp+ebx+0xee]`/`[esp+ebx+0xfe]` with `ebx` = 2*(i+1) in the
  first and `[esp+ebx+0xf0]`/`[esp+ebx+0x100]` with `ebx` = 2*i in the second.
  Both pairs are `A[i]`/`B[i]` (A at 0xf0, B at 0x100).
- `mov ebx, esp` / `mov eax, esp` pass the *first* buffer: dropping it shifts
  every esp offset by 0x50, which looks like a global-slot puzzle.
- The centred caption x lives in ECX across both setup loops (0x1a4a9 ->
  0x1a5c8); recomputing it in C re-spills it and moves the code.
- A `push 0` before a 5-argument call shifts every following esp displacement by
  4: the reads at 0x1adf7/0x1ba75 are 0x138/0x15c, not the 0x13c/0x160 they look
  like in a table sorted by printed offset.
- `cmp reg,imm; jg out; cmp reg,imm; jle loop` (0x1b156, 0x1b172) is this
  compiler's two-copy form of one spin test; write one `while` in C and the
  second load comes back. Do not read it as two different variables.

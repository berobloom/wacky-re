# Menus

Status: OPEN (the two bonus menus and the F1 controls screen are read; the main menu chain and
fn_1f858 are not).

## Summary

Two "order the bonus content" screens share one shape: they print three
26-byte message lines into 0x7c854/0x7c890/0x7c8cc, load a PCX palette from
the dat, copy 768 bytes into a local buffer, then flash palette entries with
fn_182d0 while the player moves a selection, and finally copy the worked
palette to p7d718 and set v88b9e. `fn_1832c` (0x1832c) picks a race pack and
a sound card; `fn_193a8` (0x193a8) picks a bonus zone (v88c96). Port: both in
`src/ww.c` (near misses, see the `differs:` notes there).

## Findings

- CONFIRMED (code 0x18f16-0x18f2e, 0x18f8a, 0x190ea-0x1912b): fn_1832c's input wait has two
  exits. ESC (`key88cd0 == 1`) waits for release, refreshes the palette and returns to the main
  loop (0x18843); the timeout (`ticks > eight`, `jl 0x18f8a`) runs the joystick navigation: the
  blink counter s358, the cursor keys and cfg bytes v7b6d9/v7b6db, and with joy88b40 the
  0x12 voice, then for v88bb2 == 4 `trial_book(v88bb6 + v7ecb4, v88b88, 0)` (0x1911f), back to the
  menu when v88cec is set. Until iter 38 the C sent the timeout to the main loop too, so all of that
  was unreachable and the compiler dropped it (found by tools/wc/nmrefs.py: the game calls
  trial_book, our object did not).
- CONFIRMED (code 0x1941c-0x19466 and 0x18404-0x1844e):
  both menus copy the same two 26-byte strings (" PLEASE SEE MAIN MENU TO ",
  " FIND OUT HOW TO ORDER   ") into 0x7c854/0x7c890 and differ only in the
  third line: " THESE EXTRA RACES.      " (races) vs " THESE EXTRA ZONES.      "
  (zones) at 0x7c8cc. `strcpy` with a literal is compiled to
  `mov ecx,6; rep movsd; movsw`.
- CONFIRMED (code 0x19480-0x194db): the zones menu's palette comes from one dat
  record, named "BSBCS.PCX" when v7ecb4 != 0 and "SBCS.PCX" otherwise; it seeks
  to `telldat() + datsize() - 0x300` and reads 0x300 bytes - the PCX palette
  tail - into the buffer `pal7d848` (0x7d848) points at. The races menu instead
  builds "SB<n>.PCX" names with inline strcpy/strcat chains (0x18471-0x18500, then datfind 0x18501 / telldat 0x18506 / datsize 0x1850d / seekdat 0x1851e / readdat 0x18525).
- CONFIRMED (code 0x18326): fn_182d0's 5th argument (the palette base) is pushed
  by the caller and popped by the callee, `ret 4`; the call sites therefore have
  no `add esp,4`, in the game and in our build.
- CONFIRMED (code 0x182e9 `test cx,cx`, 0x182d6 `imul esi,ebx,3`, 0x182d9-0x182e3
  `movsx edx,dx; lea eax,[edx*4]; sub eax,edx`): fn_182d0's 4th argument is a **word** (tested as a
  word; our `long a4` gave `test ecx,ecx`), its 3rd arrives in EBX and only ever used as the copy
  count `3*a3` (`cmp si,bx`/`cmp bx,si`), and its 2nd is a short sign-extended into EDX and times 3
  as `x*4-x`. Declaring `short a4` reproduced the test and left all ~24 caller bodies byte-identical
  (iter 16, WW.OBJ 183 MATCH), so the callers pass literals 0/1 in both builds.
- CONFIRMED (code 0x1956e-0x195a7, 0x19a19-0x19b63): the flash loop resets
  palette entries 5..0xa (fn_182d0 with a4=0, self-copy), then highlights entry
  v88c96-1 with a4=1 (copy from pal7d848). Inside the menu loop the entry under
  the cursor is un-flashed/highlighted again every >12 iterations, alternating
  a4 through the local at esp+0x328; each nav key flashes once, immediately.
- CONFIRMED (code 0x19aa5, 0x19b23): v88c96 is the zone selection and wraps
  6 -> 0xb and 0xb -> 6, i.e. twelve choices numbered 6..11.
- CONFIRMED (code 0x19b68-0x19bb6, 0x19c53): pressing enter (joy88b40) always
  plays voice 0x12, and the menu returns *without* setting p7d718/v88b9e unless
  netgame == 0, v7e574 != 0 and v88c96 > 7 - only that path plays voice 0x1a,
  calls showlines(3,1,0) and falls through to the exit. So zones 6 and 7 cannot
  be entered from this menu.
- LIKELY (code 0x197eb, 0x19b91: the same `> 7` test in the netgame==1 branch):
  v88c96 6/7 are the free (already ordered) zones and 8..0xb the orderable
  bonus zones. Gap: nothing in this function maps v88c96 to a race; the
  consumer is fn_1f858 (call sites 0x20286, 0x2049e), not yet read.
- CONFIRMED (code 0x196c6-0x1972e, 0x19781-0x197e5, 0x1992a-0x199a2): the two
  menus navigate differently. The races menu uses key88cd3/key88cd6; the zones
  menu uses key88cd5/key88cdd, also set from cfg7b6d8[1]/cfg7b6d8[3]
  (v7b6d9/v7b6db, 0x196d8/0x196e8) and from the peer's key byte when
  netgame == 2 (v88cef bits 1, 2, 4, 8, 0x10, 0x20, 0x40 -> key88cd5,
  key88cdd, joy88b40, joy88b42, joy88b44, joy88b46, v88cec).
- CONFIRMED (code 0x199c7-0x199fb, with wait_or_key 0x14c7c): after any input
  the menu waits up to 20 ticks, ending early when key88cd0 == 1 (which also
  sets v7ebb0 when connected). The game has wait_or_key's body inline here;
  there is no call to 0x14c7c inside fn_193a8.
- CONFIRMED (code 0x19633-0x19672): at the top of each menu iteration the game
  waits 8 ticks (or a key88cd0 press, which sets v88cec); that is the menu's
  frame limiter.
- CONFIRMED (code 0x19c68-0x19da6, `ctl_label`, called only from the "CHOOSE VEHICLE" screen at
  0x1a437 with arg1 = its `[esp]` buffer and arg2 = `[esp+0xa0]`): the screen shows the current
  player's control device in arg1 - `KEYBOARD`, `JOYSTICK #n` or `GAME PAD #n` (the number from
  `itoa(v88b7e + 1, arg2, 10)` then an inline `strcat`) - and a hint line in arg2, always
  "PRESS F1 TO VIEW YOUR CONTROLS" except in the net-game case, where arg1 is cleared with
  `strcpy(arg1, "")` and arg2 becomes "REMOTE IS CHOOSING A CAR" (0x19c98-0x19c9d, 0x19cbd-0x19cc2).
- CONFIRMED (code 0x19c98 and 0x1a3c4): `[0x600ac]` is not a variable. It is the pooled `""`
  literal, which string pooling places on the NUL of "WACK"/"ack" (0x600a4/0x600a9); every
  `mov al,[0x600ac]` + store is `strcpy(p, "")` inlined as one byte copy.
- CONFIRMED (code 0x19d2d, 0x19d3b, 0x19d49, 0x19d90): `strcpy` with a literal copies `strlen+1`
  bytes as `n/4` dwords, then `movsw` if `n & 2`, then `movsb` if `n & 1`, using `mov ecx,n/4;
  rep movsd` when `n/4 >= 4` and plain `movsd`s below it (KEYBOARD 9: movsd,movsd,movsb; JOYSTICK #
  and GAME PAD # 11: movsd,movsd,movsw,movsb; PRESS F1... 31: `ecx=7 rep movsd`,movsw,movsb). The
  game keeps three separate copies; writing the three as one `if/else if/else` chain makes our build
  tail-merge the two 11-byte ones - `if (dev) { if/else } else` keeps them separate (0x19d24-0x19d52).
- STRONGLY SUPPORTED (0x1a400 uses "PLAYER TWO" when v88b7e == 1 else "PLAYER ONE", and 0x19d5c
  prints `itoa(v88b7e + 1, ...)` after "JOYSTICK #"): v88b7e is the current player index, 0-based.
- LIKELY (code 0x19c84-0x19cbb): in a net game the "remote" slot is player index 1 when
  netgame == 1 and index 0 when netgame >= 2.
- LIKELY (code 0x19cec-0x19d28, with sound_init writing 0,0,1 to the same three words): the device
  is player 1's `v7ee4a` (0 keyboard, 1 joystick, 2 gamepad) unless cfg77128[0] != 0 forces 2; for
  player 2 in a local game it is 1 when cfg77128[2] == 0 else 0, unless cfg77128[1] != 0 forces 2.
  `ctl_label`'s 2-byte register near miss is QUESTIONS.md 2.
- STRONGLY SUPPORTED (code 0x19882, 0x19895): v7ecc4 counts menu iterations;
  after the first it is clamped to 2 and switches v7ed20/v7ed1c from 0x550/0x2a8
  to 0x88/0x220 (the net-game tick rates) - the same pair the races menu sets
  at 0x18365 to 0x440/0x2a8.

- CONFIRMED (classes_menu 0x19fc0-0x1a003 writes slots 0..8 of the same array): 0x7c854 is not
  three buffers but one array of 0x3c-byte message lines - `str7c854`, `str7c890` and `str7c8cc`
  are slots 0, 1, 2 (0x7c854 + 0x3c*i); the KID MODE help fills slots 0..8 (up to 0x7ca5c) and is
  followed by `showlines(9, 1, 0)`, as the bonus menus fill slots 0..2 and call `showlines(3, 1, 0)`.
  LIKELY showlines(count, 1, ?) draws the first `count` slots of that array; gap: showlines itself
  is not rebuilt and net_end 0x110d4 passes 4 after filling `netstats`, so which buffer it reads
  is not proven.

- CONFIRMED (0x15f98 `cmp word [esp+0xc8],0` and 0x15fad `movsx edx, word [esp+0xc8]` are the same
  slot, the frame's spill of menu's 4th argument; of the C module's 53 `msgbox` 0x2a414 call sites
  31 pass y = 0 (`xor edx,edx`), 4 pass a literal, and this is the only one that loads it
  (census over a chunked whole-module dump - `tools/le/disasm.py` stops at the first undecodable
  byte, so one long request silently undercounts); experiment:
  `tools/wc/difffn.py engines_menu` ends with `spr_y` 0x52 in the original and 0x40 in a build that
  passes the literal 0x3c there): `menu` 0x155c0's 4th argument does
  double duty - nonzero also draws 'LEE & JACKSON LAWNMOWERS' (when `netgame == 0`) at y = that same
  argument. `engines_menu` 0x167d8 is the only caller passing nonzero (0x4e, 0x168cd), so the sponsor
  banner belongs to the engine-choice menu and sits on the row its caller chose.

### The F1 controls screen (fn_1c0f4, 0x1c0f4)

- CONFIRMED (rebuild `src/ww.c`: 1944 bytes, the same size as the game's, 67 aligned instruction
  diffs): `fn_1c0f4` runs 0x1c0f4-0x1c88b (0x798) and is the whole help screen cars_menu reaches at
  0x1b2df/0x1b415. arg1 (EAX, kept in EBP) is the caller's buffer, printed at (0x10, 0x3c) at
  0x1c3af - in `cars_menu` that buffer holds only the pooled empty string, which is why the screen
  shows a blank line there.
- CONFIRMED (frame `sub esp,0x3d4` at 0x1c0fa plus every esp displacement in the body): the frame is
  twelve 80-byte slots (esp+0x000..0x3bf) and five dwords (esp+0x3c0..0x3d3). Rows 1..6 are the six
  control rows, row 7 the empty line, row 8 the giggle-keys line, row 10 the centred title/line
  buffer, row 11 the `itoa` target; rows 0 and 9 are never read or written, and the print loop
  0x1c70c-0x1c746 walks rows 1..n (n = 6, or 8 in the two-player path) at y = 0x4c in steps of 10.
- CONFIRMED (0x1c3b4/0x1c43d/0x1c47e/0x1c532): the control names are six 14-byte strings in an
  84-byte per-player record at 0x77432 (= `cfg77128` + 0x30a), indexed +0, +0x0e, +0x1c, +0x2a,
  +0x38, +0x46 in the order STEER LEFT, STEER RIGHT, ACCELERATE, FIRE WEAPONS, BRAKES, HAND BRAKE
  TURN. All zero in the image, so the names are written at runtime; who writes them is open
  (QUESTIONS.md).
- CONFIRMED (0x1c183-0x1c1ea, and compare `ctl_label` 0x19cec-0x19d28): the five frame dwords are
  this side's and the other player's device value (same three tests as ctl_label: v7ee4a and the
  shorts at cfg77128+0 for player 1, cfg77128+2/+4 for player 2), the saved `pal_d`, the player
  index (`v88b7e`, forced to 0 in a net game) and the saved `pal_a`.
- CONFIRMED (0x1c538-0x1c5f5, 0x1c64f-0x1c6e6): rows 5 and 6 (BRAKES, HAND BRAKE TURN) show the
  record's names when `vals[vals[3]] <= 1` and the literals "BUTTON3"/"BUTTON4" (0x60d20/0x60d28)
  when it is 2, and nothing at all for any other value in the single-player path; rows 7 and 8
  exist only in the `twoplayer || splitscreen` path, and the giggle-keys line says
  "1  - 6  ARE YOUR GIGGLE KEYS!" only when the player index is not 0 and `netgame` is 0, i.e. the
  second player of a local game (CONFIRMED, corrected iter 38: 0x1c60f `je 0x1c61b` -> 0x60d30
  "F1 - F6" when the index is 0, 0x1c619 `je 0x1c635` -> 0x60d50 "1  - 6" when netgame is 0; the
  earlier note had the netgame test inverted), "F1 - F6 ARE YOUR GIGGLE KEYS!" otherwise. LIKELY: 2 is the gamepad (the
  value's meaning comes from the `v7ee4a` finding above), i.e. a gamepad side is told its last two
  rows are its third and fourth buttons.
- CONFIRMED (0x1c7ae-0x1c7d6): a screenshot is taken while `key88ccd` is set, then the screen spins
  with `while (key88cd0 == 0x3b);` - 0x3b is the F1 make-code, so `key88cd0` holds a scancode, not
  a boolean (the name `aborted` on 0x88cd0 is misleading here).
- CONFIRMED (0x1c7d9-0x1c87c): after the wait, `f278c4(0)` and `f27920(0)` run; in a net game
  `v88cf0`/`v88cf3` are zeroed, `net_key(v88cf0, v88cf3, 0)` fills `net88c14`, and the screen waits
  60 ticks (`ticks` snapshot at 0x1c821). Then `pal_a = saved pal_d`, another retrace and screenshot
  pair, and `pal_d`/`pal_a` are restored.
- CONFIRMED (scan of every E8/E9 rel32 target in the code object, every `lea reg,[disp32]`, and
  every absolute dword in both objects; `tools/le/xref.py` agrees): 0x1c88c and 0x1c968 are two
  more functions in this module and nothing in the image refers to either. Both are rebuilt
  byte-identical (`fn_1c88c` 0x1c88c-0x1c964: save the three page offsets, page to `pal_d` with
  `pal_c` = 48000, `msgbox(arg1, 0)`, 300-tick wait, page back, restore; `fn_1c968` 0x1c968-0x1c993:
  `palptr = pal7d850`, `pal_a = 0`, `getpalette`, `fade_out(pal7d850)`, `v88bda = 1`). SPECULATIVE:
  the linker kept them because the module was linked whole.

- CONFIRMED (0x2bace-0x2bad6, `cmp [after],0; je 0x2bbc9` = the epilogue): the Duck Shoot screen
  `duck_screen` 0x2b5e8 called without `after` (the instructions before the shoot) returns right
  after the hall-of-fame table - no race song, no PRESS FIRE, no fade_in, no wait_input; only the
  after-the-shoot call shows PRESS FIRE and waits. The rebuild had the tail unconditional until
  iter 37; `if (!after) return;` gives the game's 1520 bytes (10 rows left, all codegen).

## `menu` 0x155c0 never ends under emulation: what makes it return (CONFIRMED, iter 75)

Every capped near miss whose run sits in a menu is inside `menu` 0x155c0 (31 direct call sites in the C
module, `classes_menu`'s at 0x19f87). Two blockers, both measured with `tools/wc/difffn.py`
(a capstone census of the C module 0x10000-0x36a4e is quoted below):

- The frame delay `while (ticks - v7ea70 < 8)`: 0x16047 loads `v7ea70` 0x7ea70 once, then 0x1604c
  loads `ticks` 0x7e720, 0x16052 `sub edx,eax`, 0x16054 `cmp edx,8`, 0x16057 `jl 0x1604c`. The
  snapshot is `ticks` itself (0x158f6 and 0x15bd9 `mov [0x7ea70],eax` after `mov eax,[0x7e720]`), and
  the C module never increments `ticks`: of its 85 stores 83 are `mov dword [0x7e720],0`, one is
  `mov [0x7e720],ebx` at 0x11166 with `ebx` known 0 (0x11151 `test ebx,ebx; jne 0x1118f`), and one is
  the race-clock restore `mov eax,[0x7ec4c]; mov [0x7e720],eax` 0x32598. The incrementer is the timer
  ISR above the C module, which difffn stubs. So **no static `SEED=` of 0x7e720 or 0x7ea70 can make
  the difference >= 8.** `TICK=8` (difffn.py, iter 75) adds 8 to 0x7e720 on every stubbed call - the
  loop tail's frame pump `call 0x37de0` 0x16004 is the stubbed call - and the delay ends.
- Without input `menu` still does not return: the key globals are set only by the stubbed asm input
  layer (C-module reads/writes: 0x88ccd 143/1, 0x88cd0 60/3, 0x88cce 21/1; and the three C writes to
  0x88cd0 - 0x15546, 0x2fd18, 0x367f3 - all store 0, as do 0x32ba4 for 0x88cce and 0x32bab for
  0x88ccd). So a seeded key flag is cleared but never re-set, and the release wait at 0x16286
  (`cmp byte [0x88cd0],1; je 0x16286`) spins.
  The way out is the joystick arm: `SEED=0x88cce=1:b` -> 0x16258 sees it nonzero, 0x16261 sets
  `joy88b40` 0x88b40 to 1, and 0x16331 `cmp word [0x88b40],0; jne 0x163df` returns the selection in
  EAX from `[esp+0xbc]`.

With `TICK=8 SEED=0x88cce=1:b` (one process per function; re-measured at `TIMEOUT=75`) the capped
lines change state as follows. Adjudicated iter 75: `classes_menu` 0x19eac SAME with both sides
returning (control with the same seeds SAME). Leads, no control run yet: `engines_menu` 0x167d8 and
`race_results` 0x2bff0 print SAME at full return, `menu` 0x155c0 SAME. `cars_menu` 0x1a380 stops at
the 300-call cap (the menu itself waits for the player) and printed **DIFF 1 byte at 0x7d838** - that
was a real bug in our C, fixed and recorded in findings/car_select.md; the same run is now SAME at the
same progress. `intro` 0x348f4, `credits_screen` 0x1efb0, `race_records_menu` 0x1d13c, `main_menu`
0x1f858, `fn_1832c` 0x1832c moved from the 3M-instruction cap to the 300-call cap (their own outer
loops). Adjudicated iter 76: `com_open` 0x102f4 SAME with both sides returning, once difffn can pass
register arguments (`ARGS=`) and execute `in`/`out` (findings/serial.md); `kart_draw` 0x24bcc SAME
with both sides returning, once the null `curkart` is seeded (findings/game-loop.md); `intro` 0x348f4
still capped but SAME at CALLS=300 and at CALLS=6000 - the 3M-instruction cap it used to report was
the wait for `ticks` > 6 (0x34cb4: no stubbed call in the body, so `TICK=` alone cannot end it;
`SEED=0x7e720=0x1000:d` does). `showlines` 0x2a268, `volume_menu` 0x1e760 and `chat` 0x14d00 still sit
at the instruction cap; `fn_193a8` 0x193a8 still crashes at the same instruction on both sides
(inconclusive; a null pointer global).

- CONFIRMED (iter 79: `SEED=0x88c96=1:w,0x88cd0=1:b,0x7ebc0=1:d tools/wc/difffn.py fn_193a8` prints
  SAME with **both sides returning**; control `NEW=wworig MAP=.work/link/WWORIG.MAP` with the same
  seeds SAME; `tools/wc/hotfn.py` with those seeds reports 45797 insns at 475 addrs and `returned`,
  so the run walks the body and its helpers, it is not a short-circuit; `TICK=` is not needed):
  the zones menu leaves its two waits on **key88cd0**, not on `ticks` - each wait zeroes `ticks` a
  few instructions before testing it (0x19633, 0x199c7) and neither loop body contains a stubbed
  call, so `TICK=` can never pay there; the 8-tick wait leaves at 0x1965e `je 0x1966b`, the 20-tick
  wait at 0x199f2, which needs both key88cd0 == 1 and EBP non-zero (`test ebp,ebp` 0x199e4
  `je 0x199f4`): EBP is `connected` v7ebc0 (loaded 0x19608/0x1962d), so seed that non-zero too.
- CONFIRMED (iter 79, the crash both builds used to share): it is `fn_182d0` 0x182d0's **source**
  pointer. 0x19591 `mov ax, word [0x88c96]; dec eax` and the helper's
  `lea eax,[edx*4]; sub eax,edx; add edx,[esp+0xc]` 0x182dc-0x182f3 make it
  `pal7d848 + 3*(v88c96-1)`; with the emulator's zero globals that is 0 - 3 = 0xfffffffd, below the
  mapped zero page, faulting at 0x182fd `mov cl, byte [edx]` - `pal7d848` 0x7d848 is 0 because the
  loader that fills it (readdat/fread) is stubbed. `SEED=0x88c96=1:w` (1 is a real zone number) puts
  the index at 0, inside the zero page, and the palette-flash calls 0x19575-0x195a7 and
  0x19a2b-0x19a5e then run identically on both sides.
- CONFIRMED (iter 79, `tools/wc/difffn.py` with its new `TICKREAD=n`): `chat` 0x14d00 **returns on its
  own** - it is not a menu waiting for a player. The only loop it never left is the per-line frame
  limiter 0x150e5 `mov eax,[0x7e720]; sub eax,edi; cmp eax,0x18; jl 0x150e5`, whose snapshot 0x150df
  `mov edi,[0x7e720]` is three instructions earlier: seeding `ticks` is swallowed by the snapshot and
  `TICK=` never fires there because the loop body has no stubbed call. `TICKREAD=8` adds 8 ticks on
  every *read* of the clock - a hook on the data address, so it covers both builds' copies of the
  loop, which sit at different addresses - and both sides then run to the return and agree (8196 insns
  at 735 addrs; control SAME; `TICKREAD=1` SAME over the same 735 addrs, so the clock speed is not
  skipping the body). Same night, both `TICK=8 SEED=0x88cce=1:b` with the control SAME: `race_results`
  0x2bff0 SAME with both sides returning (27876 insns at 644 addrs), and `menu` 0x155c0 SAME and
  returning but on only **604 insns at 406 addrs** - with zero arguments it leaves near the top, so
  that SAME says nothing about its frame wait 0x16004/0x1604c or its input arm (`ARGS=`, iter 76).
- CONFIRMED (iter 85: `menu` 0x155c0 run with its arguments set, `TICK=8 SEED=0x88cce=1:b,0x88cd0=1:b,
  0x88c46=4:w RESEED=0x88cd0=0:b CALLS=20000 COUNT=60000000`, argument shapes taken from the real call
  sites in `src/ww.c` - `ARGS=6,1,2,0` (`mode` 2, the arm that zeroes `net7ed78` at 0x155c4),
  `7,1,0,0`, `3,1,1,0` (`mode` 1), `2,1,0,0x4e`, `5,1,0,0`; the control
  `NEW=wworig MAP=.work/link/WWORIG.MAP` prints SAME for every one, and `hotfn.py` counts **14932 insns
  at 510 addrs, `returned`**, against 604 at 406 for the zero-argument call, so the frame pump and the
  input arm are inside the comparison now): every one of those runs reports exactly **one**
  data-segment difference, `palptr` 0x7d760 game `f0` / ours `fc`, and the writers are the same
  instruction on both sides - `mov dword [0x7d760], eax` at game 0x32d4c and ours 0x57d68, inside
  `fade_in` 0x32cf8, whose prologue is byte-identical in both builds (six pushes + `sub esp,0x308`).
  `fade_in` caches the address of its local `signed char cur[0x300]` in a global, so the value is a
  stack address: game 0xeffaf0, ours 0xeffafc, **12 bytes apart = `menu`'s own frame gap** (game 0xe0,
  ours 0xd4 - the `#0xd4` same-shape row note in `src/nearmiss.txt`): everything below menu's frame
  moves with it. That is same-shape, not a bug, and it is the general rule for these comparisons - a
  function that stores a local's address into a global turns a *frame-size* difference into a
  data-segment DIFF. `ARGS=2,0,0,0x4e` and `ARGS=5,0,1,0` print SAME outright (no `fade_in` on those
  paths). The `call 0: game 0x375cd(0,0,0,[88464,0,0]) ours 0x10b8f(0,0,0,[242267,0,0])` line printed
  with each DIFF is not a difference: `f375cd` 0x375cd takes no arguments (`pushal`..`popal` 0x376d5,
  findings/video.md), so those four values are registers the caller happened to leave (GOTCHAS).
- CONFIRMED (iter 85: `cars_menu` 0x1a380 - `TICK=8 SEED=0x88cce=1:b,0x88cd0=1:b,0x88c46=4:w
  RESEED=0x88cd0=0:b CALLS=20000 COUNT=120000000` - SAME with **both sides returning**, no DIFF byte,
  EAX equal and equal stub-call counts; control with the same env SAME; `hotfn.py` 18136 insns at 819
  addrs, `returned`): the iter 72/75 "cars_menu never returns, it waits for the player" was one static
  seed that both pressed and released the key. With the press/release pair the whole car-select screen
  runs - menus, slides, the three selection-box draws, msgbox - and leaves the data segment identical,
  so this line's 445 rows are byte-level homes with no behavioural divergence behind them
  (findings/car_select.md).

- CONFIRMED (0x1efb0-0x1f43b executed under `tools/wc/difffn.py`, iter 77): `credits_screen` ends by
  itself - it is six pages, the loop back edge is 0x1f428 `cmp dword [esp],5; jle 0x1f037` and the
  return is 0x1f432, so a capped run is a wait, not a menu that needs the player. The wait is the
  per-line frame delay 0x1f305 `cmp edi, dword [0x7e720]; jge 0x1f305`: `edi` is the `pal_a` 0x88b24
  **word** loaded at 0x1f064 and 0x1f3d1 (0xbb80 / 0x7d00), and the page head 0x1f05a stores 0 to
  `ticks`, so `SEED=0x7e720=` cannot help (it is wiped every page) and the loop body has no stubbed
  call, so `TICK=` only pays at the next `f37de0` 0x1f2e0. `TICK=65536` (one add per stubbed call,
  more than any word deadline) passes it at once; `TICK=8` needs > 2000 calls. The six pages take
  between 2000 and 20000 stubbed calls, hence `CALLS=20000`: with that both sides of
  `difffn.py credits_screen` reach the return and agree (control with the same env SAME, and SAME
  again with `SEED=0x88cce=1:b`, which takes the early page tail 0x1f374 `cmp byte [0x88cce],0;
  jne 0x1f3c3`).
- CONFIRMED (0x2a268-0x2a410, iter 77): `showlines(n, mode, deadline)` has two tails and neither can
  be reached with difffn's default zero arguments. The line loop ends at 0x2a382 `cmp edi,
  [esp+0xc]; jle 0x2a363` (so n=0 prints nothing); 0x2a384 `cmp dword [esp+8],0` picks the tail.
  `mode == 0` goes to 0x2a3ab `mov [0x7e720],0` then waits `cmp ebp, [0x7e720]` at 0x2a3b5/0x2a3bd
  against **arg3** (EBX, 0x2a277 `mov ebp, ebx`) - the real call sites pass 0 (0x113f1, 0x18dd7,
  0x19baa, 0x1a084, 0x1b4d3), 0x198 (0x20dd2) and 0x220 (0x2c89e), and since `ticks` is zeroed first
  and the wait body has no call, no seed and no `TICK=` ends it. `mode != 0` goes to
  `wait_release` 0x278c4, `wait_input` 0x27920 with EAX=1, and stores the answer into `v88b9e` 0x88b9e
  (0x2a39c) - that path runs and returns with `ARGS=3,1,0 SEED=0x88cd0=1:b` (key88cd0==1 is
  `wait_input`'s exit at 0x27984 -> 0x2798d `mov eax,1`), and both sides of `difffn.py showlines`
  then agree through the return (control SAME). `ARGS=` takes the Watcom registers in order, so this
  is eax=3, edx=1, ebx=0.

- CONFIRMED (0x1e760-0x1eee1 executed under `tools/wc/difffn.py`, iter 78): `volume_menu` has three
  waits and difffn can leave none of them, but two of them no longer stop the comparison.
  (a) In the default image `musicready` 0x7ed4c = 0 and `sfxsoft` 0x7ed48 = 0, so 0x1e76c/0x1e775 take
  the `BOTH SOUND DEVICES SET TO NONE!` branch: `call fn_1c068` 0x1e77e, `msgbox` 0x1e78a, then
  **`mov [0x7e720],0` 0x1e78f** and the wait 0x1e7af `cmp dword [0x7e720],0x12c; jle 0x1e7af`.
  `SEED=0x7e720=` is wiped by 0x1e78f and the loop body holds no call, so `TICK=` never pays: that
  branch (which does return, 0x1e7c0 -> 0x1eed5) cannot be run to its end. Seed `0x7ed4c=1:d` instead
  and the function takes its normal path at 0x1e7c5.
  (b) The menu loop 0x1eada-0x1ee9d has a frame-rate limit 0x1ecac `while (ticks - v7e724 < 0xe)`
  whose snapshot `v7e724` is `ticks` read at 0x1eadf; between them sit the two stubbed pumps
  `drawsprite` 0x1ec64 and `f37de0` 0x1ec69, so `TICK=8` pays it every pass. With
  `TICK=8 SEED=0x7ed4c=1:d CALLS=3000` both sides of `difffn.py volume_menu` are SAME over 3000
  stubbed calls (~300 redraws) and the control (game vs game, same env) is SAME too; neither side
  returns, because the only exit is (c).
  (c) The exit is a key press **and** release: 0x1ecd2 `cmp byte [0x88cd0],1; je 0x1eea2`, and 0x1eea2
  `cmp byte [0x88cd0],1; je 0x1eea2` waits for the same global to be clear again. One static seed
  cannot do both, so `SEED=0x88cd0=1:b` moves the park from the menu loop to 0x1eea2 (hotfn.py:
  18942 passes) - still capped, still SAME. The tail it is trying to reach (0x1eeab `FX_StopAllSounds`
  if `sfxsoft`, `eax = 1`, `save_config` 0x1eebe, then `v88bda = v88b9e = 0` and the epilogue 0x1eed5)
  stays unexecuted for this function.
  (d) Inside the loop the navigation is four key globals read without any wait: `key88ccd` 0x88ccd
  (screenshot, then the release wait 0x1ec85 `cmp byte [0x88ccd],0; jne 0x1ec85` - seeding that one
  parks the run there), `key88cd5` 0x88cd5 and `key88cdd` 0x88cdd (volume -5/+5 with the
  `[esp+0x128]` clamp at 0x1ed50/0x1ed87), `key88cd3`/`key88cd2` 0x88cd3/0x88cd2 (row up/down,
  0x1ee73-0x1ee9d).

- CONFIRMED (whole span 0x1f858-0x2130f disassembled, iter 78): **`main_menu` contains no `ret` and no
  direct jump out of its span.** Its only return is indirect: the switch head 0x1fa6b `dec esi;
  cmp esi,8; ja 0x1f878` and 0x1fa75 `jmp dword ptr cs:[esi*4 + 0x1f824]`, whose 13 dwords (read out of
  `.work/le/ww.obj1.bin`: 0x1fb0e, 0x1f878, 0x200f2, 0x2037c, 0x205b9, 0x210ba, 0x20df5, 0x212fe,
  **0x16978**, 0x20f43, 0x20f5b, 0x20f73, 0x21099) sit in the gap just before the function; slot 8 =
  selection 9 = LEAVE GAME is the single entry that leaves the span, and it lands on
  `engines_menu`'s 9-byte epilogue at 0x16978 (`add esp,8` + pop ebp/edi/esi/edx/ecx/ebx + ret), the
  tail the linker shared with it (iter 66). Slot 1 (selection 2) and the out-of-range arm 0x1fa6f both
  land on 0x1f878, the loop back edge that the seven `jmp 0x1f878` sites share. So a capped
  `difffn.py main_menu` is not parked in a wait of its own: with `TICK=8 SEED=0x88cce=1:b` it is inside
  a submenu at the call cap (hotfn.py counts 8629 instructions at 661 addresses in the first 300 stubbed
  calls) and reaches the cap at 20000 calls, SAME on both sides, control SAME.
  To reach the return the menu has to hand back 9, and `menu` produces 9 only at 0x162b8-0x162c6 - the
  arm `v88bb0` in {1,0x10} with `v7ecc0` != 0, which returns the **incoming** cursor argument
  (`mov eax,[esp+0xcc]; mov [esp+0xbc],eax`) - while `v7ecc0` is written 1 only at 0x16273, under
  `key88cd0` == 1 tested at 0x1626a, three instructions before that same key's release wait 0x16286
  `cmp byte [0x88cd0],1; je 0x16286`; the other arm 0x16291 stores `v7ecc0 = 0`, which erases a seed.
  Same press-and-release contradiction as `volume_menu` (d) above, so `main_menu` stays capped under one
  static seed.

### The input pair `wait_release` 0x278c4 / `wait_input` 0x27920 sets the ceiling on a seeded run (CONFIRMED, iter 77)

Both take their mode in EAX (`0x278c6`/`0x27922` `mov edx, eax`). Every one of the ten
`wait_release` call sites in the C module passes 0 (`xor eax,eax` immediately before it: 0x18765,
0x18e97, 0x1c7d7, 0x1cc2a, 0x1d7b0, 0x1da9c, 0x2a38d, 0x2f093, 0x3474b, 0x3690b), so the key test at
0x278cf is dead there; `wait_input` is called with 0 or 1 (0x1c7ea, 0x1c801, 0x2bba2, 0x2bf24,
0x2bfc5 pass 0; 0x17f24, 0x17fa0, 0x1da77, 0x2a397, 0x36917 pass 1), so the mode-2 arm 0x27948/0x2796f
is not reached from this module either. Under `tools/wc/difffn.py` the input globals are only ever
what `SEED=` left them, so:

- `wait_release` with mode 0 skips the key test (0x278ca `test dx,dx; je 0x278d8`) and returns through
  0x2791c **only when `v88cce` 0x88cce is 0 and `joy88b40/42/44/46` 0x88b40/42/44/46 are all 0**
  (0x278e7 and 0x278f8-0x2791a compare them against `ebx`, which 0x278c8 zeroes). If `v88cce` is
  nonzero it stores `joy88b40 = 1` at 0x278ef and then compares EBX=0 with it at 0x278f8, `jne
  0x278ca` - a pump of `call 0x377ae` per iteration that can never be seeded out of (119700 iterations
  counted in `race_records_menu` 0x1d13c).
- `wait_input` needs one of those flags to be nonzero: with mode 1 it returns EAX=1 at 0x27984 ->
  0x2798d when `key88cd0` 0x88cd0 == 1, with mode 2 (no C-module caller) it would return at 0x2796f
  when `ticks` >= 0x440 - that loop does call `0x377ae`, so `TICK=` ends it - and otherwise it re-pumps
  0x2795a forever. Mode 0 can be exited only by `v88cce != 0` (0x27995 -> 0x279e2, EAX=0) or a nonzero
  joy flag (0x279ba-0x279e2).
- So the pair is a contradiction under one static seed: a menu needs `joy88b40` or `v88cce` set to
  return (menu 0x155c0: 0x16258 -> 0x16261 -> 0x16331) and a mode-0 `wait_release` needs both clear.
  Measured stop points (a `Run.go` probe printing `done`/`calls`/EIP, iter 77): `fn_1c0f4` 0x1c0f4 with
  no seeds sits in `wait_input`'s pump - EIP 0x27969, i.e. its own call at 0x1c7ea (mode 0); with
  `SEED=0x88cce=1:b` that call returns and its earlier one at 0x1c7d9 (also mode 0, so the key test is
  skipped) takes the 0x278ef store instead. `race_records_menu` 0x1d13c with that seed gets menu to
  return and then sits in the `wait_release` inside fn_1d704 0x1d704 (return address 0x1d7b5, mode 0
  from 0x1d7ae). `race_records_menu`'s exit arm - 0x1d2f1 `cmp dword [0x7ecc0],0` -> 0x1d2fa
  `v88b9e = 1` -> 0x1d312 `cmp edi,4` -> return at 0x1d32b - is not seedable either: menu stores 0 to
  `v7ecc0` 0x7ecc0 at 0x15613 and 0x16291 (write hook on 0x7ecc0, iter 77).
- The pair is **not** a ceiling after all: `RESEED=0x88cce=1:b` (iter 81, `tools/wc/difffn.py` and
  `hotfn.py`) writes 1 into `v88cce` on every READ of it but the first, so the mode-0
  `wait_release` returns on the image's 0 and the following mode-0 `wait_input` sees 1 at 0x27995 and
  exits at 0x279e2 with EAX=0. `difffn.py fn_1c0f4` then prints **SAME with both sides returning**
  (control `NEW=wworig MAP=.work/link/WWORIG.MAP` with the same env SAME; `hotfn.py` reports 5525
  insns and `returned`), and the last blocks 0x1c7ef-0x1c88b are covered. Writing on the first read
  instead moves the spin one wait earlier (hot loop 0x278ca-0x278ff, `wait_release`'s pump) - the READ
  hook fires before the value is fetched, and unicorn 2.1.4's python wrapper has no
  `UC_HOOK_MEM_READ_AFTER` (commented out of its handler table; `hook_add` raises UC_ERR_ARG).
- CONFIRMED (0x1c7de, 0x1c821-0x1c831): fn_1c0f4's second tail wait compares `ticks` with itself -
  0x1c821 loads `dword [0x7e720]` into EAX once, the loop body 0x1c826 reloads it into EDX, `sub
  edx,eax`, `cmp edx,0x3c`, `jl 0x1c826` - and is reached only on the net-game side of 0x1c7de `cmp
  word [0x88c0c],0`; with netgame 0 the `jmp 0x1c833` at 0x1c7ef skips it. Nothing in that body calls
  the stub, so `TICK=` never advances it: another wait no seed ends (its difference is always 0).

- CONFIRMED (iter 82): a third input primitive, the clib `getch` (game 0x42c4d, hybrid public `getch_`:
  WWORIG.MAP `0001:00032c4d`, the hybrid map `0001:0000c20f`), is called from five C-module functions -
  `enter_name` 0x1cb44 (0x1cd88), `kbd_on` 0x32b14, `waitkey` 0x341d8 (twice), fn_35bb0 0x35bb0 (four) and
  `main` 0x35ca4 (three) - the Ghidra `FUN_00042c4d` call sites at ww.c lines 9444, 24156, 25444, 25448,
  26893, 26911, 26919, 26923, 27138, 27233, 27236. `enter_name`'s input pump 0x1ccca-0x1cdff tests **no
  global**: it leaves only when the character is 0xd or 0x1b (0x1cd8f / 0x1cd97 `cmp al,..; je 0x1ce04`),
  so `SEED=`/`TICK=`/`RESEED=` cannot end it - what is capped is the stub's constant 0. `KEY=` in
  `tools/wc/difffn.py` (iter 82) returns the list's characters, one per `getch` call, the last one
  repeating; `KEY=0x61,0x62,0x63,0x30,0x20,0x08,0x08,0x43,0x0d` walks the upcase fold 0x1cda3
  `sub bl,0x20`, the accept ranges 0x41-0x5a / 0x20 / 0x30-0x39 (0x1cdc2-0x1cdd8) and the backspace arm
  (0x1cda6 `cmp bl,8` with `test di,di` 0x1cdab, `dec edi` 0x1cdb0 and the 0x20 store 0x1cdb9), and the
  trailing 0x0d returns through 0x1ce04 `call kbd_on` + `add esp,0x74`. `difffn.py enter_name` prints SAME
  with both sides returning (control SAME; hotfn 24680 insns at 435 addrs, `returned`).
- CONFIRMED (iter 82, `SEED=0x88cd0=1:b,RESEED=0x88cd0=0:b` -> `difffn.py fn_1d704` SAME with both sides
  returning, control SAME, hotfn 1270 insns at 396 addrs): `fn_1d704` is the input pair in its press-then-
  release shape - `wait_input` with mode 1 at 0x1da77 returns EAX=1 only while `key88cd0` == 1 (0x27984 ->
  0x2798d), and the loop right after it, 0x1daaa `cmp byte [0x88cd0],1; je 0x1daaa`, waits for the same
  byte to be 0 and calls nothing. One static seed gives one or the other (measured: 749369 passes of
  0x1daaa inside the 3M-instruction cap). `RESEED=` on the same address ends both: the mode-1 test reads
  the seeded 1, the release wait reads the injected 0. The other flag, `RESEED=0x88cce=1:b`, is wrong for
  this function - it parks the run in `wait_release`'s pump, because 0x278ef then stores joy88b40 = 1
  (19963 passes of 0x278ca-0x278ff).

- CONFIRMED (iter 83: `SEED=0x88cd0=1:b,RESEED=0x88cd0=0:b CALLS=3000 tools/wc/difffn.py trial_book`
  prints **SAME with both sides returning**; control `NEW=wworig MAP=.work/link/WWORIG.MAP` with the same
  env SAME; `hotfn.py` 42703 insns at 919 addrs and `returned`; with `ARGS=3,1000,0` - the three
  arguments of the only call site, 0x1911f `trial_book(v88bb6 + v7ecb4, v88b88, 0)` - 43194 insns at 928
  addrs, also SAME/returned): `trial_book` 0x2e4b8's wall is its **own input pump** 0x2f2fa-0x2f395, not
  the pair. Each pass stores EBX (0, from 0x2f2f8) into `joy88b40/42/44/46` 0x88b40-0x88b46 and `v88cec`
  0x88cec, calls `f377ae` 0x377ae, and re-tests `v88cce` 0x88cce (0x2f32d), `key88cd0` (0x2f337, against
  ECX = 1 from 0x2f2f3) and `key88ccd` 0x88ccd (0x2f347, then `call screenshot` 0x2f34f and the bare wait
  0x2f35c). EDX is 0 from 0x2f305 `mov edx,ebx` unless one of those tests mismatches, which sets it to
  ECX (0x2f335, 0x2f33f, 0x2f388); `test edx,edx; je 0x2f2fa` 0x2f393 re-pumps (2841 passes at
  CALLS=3000), and 0x2f38a `cmp dword [ticks],0x22` only redirects a **zero** EDX to the page redraw
  0x2f098 - the exit needs an input flag, never a clock. `key88cd0 = 1` sets EDX at 0x2f33f (measured x1
  there, 0x2f335 x0) and this function's own release wait 0x2f3a3 `cmp byte [0x88cd0],1; je 0x2f3a3`
  needs the 0 back, so it takes the same `SEED=`/`RESEED=` pair as `fn_1d704`. Walked return chain
  (address counts, `.work/fncover.py`, each x1): 0x2f38a -> 0x2f39b -> 0x2f3a3 -> `wait_release` mode 0
  at 0x2f3ae (its 0x278e7 test sees 0 and 0x2f307-0x2f31c has just zeroed the joy flags) -> `getpalette`
  0x2f3bd -> `fade_out` 0x2f3c7 -> `f37de0` 0x2f3d5 -> 0x2f3da `cmp byte [key88ccd],0` -> `add esp,0x15c`
  + 5 pops -> the **only `ret`** 0x2f404 (whole-range disasm of 0x2e4b8-0x2f407: one `ret`, one jump-table
  jump 0x2e533). Not walked: the record block behind 0x2f193 `cmp dword [esp+0x144],0; je 0x2f2d5` (0x2f1d9
  and its three call-free ticks waits 0x2f1fe/0x2f25e/0x2f2b8 measured x0 - `TICKREAD=` is the knob for a
  wait whose body calls nothing) and the laptimes block 0x2e681-0x2e960, which the game's own call skips
  (`arg3` = 0, 0x2e689 `je 0x2e973`).
- CONFIRMED (iter 89: `ARGS=3,1000,1 SEED=0x88cd0=1:b,0x88c46=4:w RESEED=0x88cd0=0:b TICKREAD=8
  CALLS=20000 tools/wc/difffn.py trial_book` prints **SAME** at CALLCAP 20000/20000; control
  `NEW=wworig MAP=.work/link/WWORIG.MAP` with the same env SAME; `ARGS=3,1000,2` and `4` the same): the
  third argument is `show` (`void trial_book(int a, int b, int show)`, src/ww.c 16128) and it is stored
  into the local that gates five arms - `mov dword [esp+0x124], ebx` 0x2e4c6, tested at 0x2e681, 0x2e9ca,
  0x2ec06, 0x2edcd and 0x2ef4a - so `ARGS=<a>,<b>,1` (Watcom EAX, EDX, EBX) opens the lap-record arms the
  0x1911f call skips; the game does use 1 (src/ww.c 17626), so those arms are live code, not dead.
- LIKELY (iter 89, the new blocker with `show` != 0): the arms print strings the harness has not seeded -
  the text loop 0x149df-0x14af3 (`cmp byte [edx],0; jne 0x149df`, `add [0x88b32],8` per character) walked
  35649 characters in one pass, and 1500001 executed instructions bought only 113 of the function's own
  instruction starts. The un-terminated strings are the 0x78-byte records at 0x70730 (trial_book's record
  rows, names included) rather than anything the game drew; the game's own `show` = 1 call has real rows
  there.
- CONFIRMED (`tools/wc/fncov.py FN=trial_book LO=0x2e4b8 HI=0x2f408`, iter 89): the two SAME runs of iter
  83 (`3,1000,0|0x88cd0=1:b` returned, `3,1000,5|0x88cd0=1:b` capped) cover **561 of 1034 instruction
  starts** in 0x2e4b8-0x2f408, 473 never executed; adding the `show` != 0 run leaves the union at 561. The
  record block's gate local `[esp+0x144]` is `newlap` (stored only at 0x2f076), so that block needs the
  lap minimum of 0x2e68f-0x2e6b2 (`laptimes` 0x7d358, `cmp edx,5` 0x2e6b2) to beat the record table: seed
  `laptimes` and give 0x70730 NUL-terminated names.
- CONFIRMED (iter 83): the wrong-flag trap again, by a new route - `SEED=0x88cce=1:b` instead of
  `key88cd0` puts the seeded 1 into `wait_release`'s test 0x278e7 **before** the pump's own read of it:
  0x278ef runs and its `joy88b40 = 1` store is never undone by the injected 0, so the hot loop becomes
  0x278ca-0x278ff (2844 passes at CALLS=3000) and the pump is not entered at all. Read the hot-address
  list before choosing the flag: seeding a global a library wait loop also tests can be worse than
  seeding the one the function's own loop tests.

- CONFIRMED (iter 80: `SEED=0x88cd0=1:b,0x7ebc0=1:d tools/wc/difffn.py fn_1832c` prints **SAME with
  both sides returning**; control `NEW=wworig MAP=.work/link/WWORIG.MAP` with the same seeds SAME;
  `hotfn.py` with those seeds reports 45646 insns at 617 addrs and `returned`): the races menu's wait
  0x18c96-0x18cbb has three tests - `cmp byte [key88cd0],1`, `cmp dword [connected v7ebc0],0` and
  `cmp dword [ticks],0x14` - and the body calls nothing, so `TICK=` cannot advance it and 0x18c82
  zeroes `ticks` a few instructions before the loop (`ebp` = the constant 8 from 0x18356, and that is
  also why `TICK=8` alone never returns: the store 0x18843 then the frame pump 0x18860 leave 8 in
  `ticks`, so 0x18896 `cmp ebp, [ticks]; jle 0x188b8` takes the not-ESC path and `v88cec` stays 0). `key88cd0` alone is not enough: the `connected` test 0x18c9f sends the run
  back to 0x18c96, so seed both, exactly like `fn_193a8` (iter 79). The exit is 0x18ca8
  `v7ebb0 = 1` -> 0x18cbd, and the return path is 0x19369 `cmp byte [v88cec],0; je 0x18843` with
  `v88cec` set at 0x188b1, then the 0x300-byte palette copy to `p7d718` 0x19378-0x1938e,
  `v88b9e = 1` 0x19390 and `ret` 0x193a5 - the menu's **only** `ret`.
  Still uncovered: the timeout side (the joystick nav 0x18f8a-0x1912b, `trial_book` 0x1911f) - with
  `TICK=8 SEED=0x88cd0=1:b,0x7ebc0=1:d` both sides walk it but stay in the menu to a 5000-call cap,
  and the only reported DIFF byte is the injected clock `ticks` 0x7e720 itself (game 08, ours 10):
  the end value of a counter the harness drives is the cap's position, not a behaviour difference.

- CONFIRMED (iter 84: `TICK=8 SEED=0x88cd0=1:b,0x88c46=4:w RESEED=0x88cd0=0:b` plus that line's own
  flag seeds makes `difffn.py` print **SAME with both sides returning** for the three menu lines that
  said `still capped` - `volume_menu` 0x1e760 (`SEED=0x7ed4c=1:d`, hotfn 2185 insns at 379 addrs),
  `main_menu` 0x1f858 (`SEED=0x88cce=1:b`, 5939 at 675) and `race_records_menu` 0x1d13c
  (`SEED=0x88cce=1:b,0x7ecc0=1:d`, 2892 at 616); control `NEW=wworig MAP=.work/link/WWORIG.MAP` with
  the same env SAME for all three): the missing half was again one flag being both press and release
  (iter 82) - `volume_menu`'s release wait 0x1eea2 and `menu`'s 0x16286 read `key88cd0` after the
  press read that sets `v7ecc0` 0x16273 - and one global the runs had never set: **`v88c46` 0x88c46,
  the fade step `main` stores at 0x36334** (findings/formats.md). Without it `fade_out` 0x1bf94 marks
  every one of the 768 entries changed (`xor edi,edi` 0x1c04d, `test di,di; je` 0x32ce2) and the menu
  spins in the palette pass instead of walking: that is the wall `intro` 0x348f4 and `demo_mode`
  0x1e2e4 sit on, and with `SEED=0x88c46=4:w` **intro returns on both sides** for the first time.
  `demo_mode` with it plus `SEED=0x7d998=2:d` now returns too (iter 84b: SAME, control SAME, hotfn
  458394 insns at 3709 addrs) - and its abort arm is the counter-example to the press/release pair:
  0x31eae `cmp byte [key88cd0],1` must see 1 on **every** read, so `RESEED=0x88cd0=0:b` there puts the
  race back on the cap (SAME but `CALLCAP 6000/6000`, neither side returns).
  `intro`'s verdict is `DIFF same-shape`, not a bug: all 123 DIFF bytes are inside the harness's
  0x0-0x10000 zero page, written through the palette pointers `main` takes from `malloc` (stubbed, so
  0): `v7d858` 0x7d858 (APOG1/BEAVIS, pal_6bit 0x26ecc) and `v7d85c` 0x7d85c (WINTRO). Pointing one
  of them at the scratch page (`SEED=0x7d858=0x8000:d`) makes the control SAME and leaves the rest as
  page-0 scribble; the 11-call gap is that same palette, `fade_in` 0x32cf8 stepping 19 times in the
  game and 6 in our build over garbage, because `fade_in` itself is SAME run on its own
  (`SEED=0x88c46=4:w ARGS=0x8000`, both sides return, control SAME).

## Traps

- `fn_1c0f4` prints the caller's buffer at (0x10, 0x3c) (0x1c3a2 `mov ebx,ebp`): a port that hands
  it a string the game never fills adds a line to the help screen.
- The help screen's rows are 80 bytes apart and the array has twelve of them; the frame size is
  what pins that (0x3d4 = 12*80 + 20). Declaring nine rows plus separate buffers gives 0x384 and
  moves every displacement.
- fn_182d0 takes 4 register arguments plus one on the stack and pops it itself;
  reading a call site without checking that makes the stack slot map of the
  caller look wrong (the caller's locals are one frame offset after the push,
  e.g. `mov [esp+0x32c],0` writes the game's local at frame+0x328).
- The zones menu returns *without* p7d718/v88b9e on most enter presses; a port
  that always sets v88b9e lets the player "enter" a zone they cannot order.
- The two menus look identical in a decompiler listing but use different key
  globals (key88cd3/cd6 vs key88cd5/cdd); Ghidra names neither.
- `ctl_label` writes the device name into arg1 and the hint into arg2, and in the net-game branch
  it *clears* arg1 (0x19c98). A port that reuses one buffer, or leaves the old label in place,
  shows "KEYBOARD" over a remote player's slot. The caller keeps two buffers, `[esp]` and
  `[esp+0xa0]` (0x1a3b5-0x1a3d1).

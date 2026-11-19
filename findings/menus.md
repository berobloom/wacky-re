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

With `TICK=8 SEED=0x88cce=1:b` (one process per function) these capped lines now return, both sides
(`classes_menu` 0x19eac adjudicated this iteration, its control with the same seeds is SAME; the other
three are leads, no control run yet): `engines_menu` 0x167d8, `race_results` 0x2bff0, `menu` 0x155c0
SAME; `cars_menu` 0x1a380 returns with **DIFF 1 byte** - a real candidate bug for the next iteration.
`intro` 0x348f4, `credits_screen` 0x1efb0, `race_records_menu` 0x1d13c, `main_menu` 0x1f858,
`fn_1832c` 0x1832c moved from the 3M-instruction cap to the 300-call cap (their own outer loops);
`com_open`, `showlines`, `volume_menu`, `chat`, `kart_draw`, `fn_193a8` still sit at the instruction
cap - `kart_draw` and `fn_193a8` crash at the same instruction on both sides (inconclusive).

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

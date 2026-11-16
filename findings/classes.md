# Classes and game modes

Status: OPEN (the per-class users after `classes_menu` and the values at `tbl7e730` are not
settled; the class menu itself is rebuilt, `classes_menu` 0x19eac in `src/ww.c`, one store
out of place; the lap menu 0x1a148 and the pre-`cars_menu` setup 0x1a2f8 are byte-identical)

## Summary

`v88bb2` (0x88bb2) is the class/game-mode index chosen by the menu at 0x19eac:
1 AMATEUR, 2 PRO, 3 CHAMPION, 4 TIME TRIAL, 5 KID MODE. `fn_19db8` (0x19db8)
maps 1..4 to three globals: the class line `cls_name` (0x6aa40), `v7ece0`
(a per-class value) and `v88b84` (an index into the word table at 0x7e730).
`v88bb2 == 4` is the time trial that `laptimes`/`calc_places` compare against.

## Findings

- CONFIRMED (disassembly 0x19db8-0x19ea8; jump table 0x19da8 =
  [0x19e02, 0x19e37, 0x19e6f, 0x19dde], index `v88bb2 - 1`): `fn_19db8` sets,
  per class, `v7ece0`, `cls_name` and `v88b84`, and leaves `v88b84` untouched
  when `v88bb2` is outside 1..4 (the default target 0x19e9e is the store of the
  loaded old value): AMATEUR → 8, "AMATEUR. " (0x6094c), 0x59 + (v7ee5a != 1);
  PRO → 4, "PRO. " (0x60958), v7ee5a == 1 ? 0x5e : 0x5c; CHAMPION → 1,
  "CHAMPION. " (0x60960), v7ee5a == 1 ? 0x63 : 0x5e; TIME TRIAL → 20, ""
  (the pooled NUL 0x600ac), 0x54. Port: `src/ww.c` `fn_19db8` (near miss, one
  register home, see the `differs:` mark).
- CONFIRMED (the 5 row literals 0x6096c/0x60980/0x60994/0x609a8/0x609bc copied
  into `mnames` 0x7bc3c + 0x3c*i in 0x19eac; `cmp word[0x88bb2], 5` at 0x19fb2
  guards the " THIS MODE IS FOR YOUNG CHILDREN " help block; `menu()` returns a
  1-based index - the matched `zones_menu` 0x16664 tests `r == 2` for its second
  row): `v88bb2` is the 1-based index into that row list, so 4 = TIME TRIAL and
  5 = KID MODE. This confirms the earlier SPECULATIVE reading of
  `v88bb2 == 4` as the time trial in `laptimes` 0x7d358 and `calc_places`
  0x128a4, and shows KID MODE has no class line and no `v7ece0` setting.
- CONFIRMED (0x28b9f: `mov ebx,7; sub ebx,eax; add ebx,[0x7ece0]; cmp ebx,[esp]`;
  and 0x28de6 `add eax,[0x7ece0]`): `v7ece0` is added to (7 - eax) in the
  functions at 0x28b20 and 0x28cac. LIKELY a score bonus; easier class = bigger
  bonus (8/4/1), time trial 20. Gap: eax's provenance (LIKELY a place) not traced.
- CONFIRMED (0x28bb1 `movsx edx,[0x88b84]`, 0x28bbb `movsx edx,[edx*2+0x7e730]`):
  `v88b84` is a signed index into a word table at 0x7e730; that table is all
  zero in the image, so it is filled at run time (SPECULATIVE what it holds).
- STRONGLY SUPPORTED (0x1e501 sets 0x59 and 0x1e58f clears `cls_name`; the
  AMATEUR/PRO/CHAMPION pairs differ only in the second value, selected by
  `v7ee5a`): the pairs (0x59,0x5a), (0x5c,0x5e), (0x5e,0x63) select the
  fast/slow engine variant - `v7ee5a` is the engine choice set by
  `engines_menu` 0x167d8 (" FAST 12 HP ENGINES "/" SLOW 6 HP ENGINES ",
  findings/menus.md).
- CONFIRMED (experiment: `src/check.sh` on the build that adds these three
  literals): literals added with functions in address order land at the game's
  CONST addresses - our "AMATEUR. "/"PRO. "/"CHAMPION. " got offsets
  0x948/0x954/0x95c, i.e. 0x6094c/0x60958/0x60960 with the module's CONST base
  0x60004 that check.sh reports.

- CONFIRMED (rebuild: `classes_menu` 0x19eac in `src/ww.c`, 667 code bytes with one store out of
  place; the row literals 0x6096c..0x609bc and the nine help literals 0x609d0..0x60af0 verified
  by the single `segment CONST base 0x060004` line `src/check.sh` prints): the class/mode screen.
  `v88bb0 = 6` (the row style `menu()` draws with), `v88b9e = 0`, `v7ecf0 = -1`, `v7ecc8 = 1`
  (0x19f4b, 0x19f55), and `menu(nrows, 0, 0, 0)` where `nrows` is a `short` local held in EDX
  (0x19f22 `mov edx,4`, 0x19f43 `mov edx,5`, used at 0x19f75 `movsx eax,dx`): **5 rows, or 4 when
  `netgame != 0`**, where the KID MODE row is not copied in (0x19f2a `cmp word[netgame],0`).
- CONFIRMED (0x19eb1-0x19ec6, 0x19f5f-0x19f6f, 0x1a107): the menu is skipped when `twoplayer != 0`
  or `netvoices != 0` - the local is `two = twoplayer; if (!two) two = netvoices;` - and then
  `v88bb2 = 4` (TIME TRIAL). So a two-player or net-voice game never picks a class: it is TIME
  TRIAL by construction.
- CONFIRMED (0x1a0a2-0x1a0cb): with `splitscreen != 0` a menu is shown only when `splitscreen == 2`
  and `v88b7e == 1` (player two's turn), and then `menu(3, 0, 0, 0)` with the rows written at
  0x19ed5-0x19f29, i.e. **only AMATEUR / PRO / CHAMPION are offered in a two-player split screen**;
  TIME TRIAL and KID MODE are not reachable there. After the choice, `netgame != 0 && net88c14`
  (the remote holds the menu) leaves the function without the tail below.
- CONFIRMED (0x19fb2 `cmp word[v88bb2],5`, 0x19fc0-0x1a088): choosing KID MODE copies nine 34-byte
  help lines (literals 0x609d0+0x24*i) into the line array at `str7c854` + 0x3c*i, i = 0..8, then
  sets `v88bb2 = 1` (the child drives as AMATEUR) and `v7edb0 = 1`, then `showlines(9, 1, 0)`;
  the text says the child only steers and the computer fixes the speed. The `v7edb0 = 1` beside
  `v88bb2 = 1` supports the names file's reading of `v7edb0` as the "engines locked" flag
  (STRONGLY SUPPORTED there).
- CONFIRMED (0x1a110-0x1a137): the tail runs when `twoplayer || splitscreen || netvoices` and
  stores 0 to `v684e0` (0x684e0) and `v685e4` (0x685e4); `return` from inside the menu paths skips
  it (all four returns jump to the pop sequence at 0x1a141).
- CONFIRMED (0x1a141-0x1a146, the caller 0x1bd3c): the function returns nothing; `cars_menu` calls
  it, then re-tests `v88b9e` before calling 0x1a148, so `v88b9e` is how the menu chain aborts.
- CONFIRMED (rebuild `laps_menu` in `src/ww.c`, 430 bytes byte-identical at 0x1a148; row literals
  0x60b14/0x60b28/0x60b3c read out of the image): 0x1a148 is the **lap-count menu**, the next screen
  of the same chain - `cars_menu` calls `classes_menu` 0x19eac at 0x1bd3c, `laps_menu` at 0x1bd4b
  and `engines_menu` 0x167d8 at 0x1bd5f, each guarded by `v88b9e == 0` (0x1bd32, 0x1bd41, 0x1bd50).
  Its three rows are " SIX LAP RACE   ", " EIGHT LAP RACE ", " TEN LAP RACE   " into
  `mnames` + 0x3c*i with `v88bb0 = 0xd` and the cursor `v88c80 = 1` (0x1a1bb, 0x1a1c4); the rows are
  1-based as always, so the switch at 0x1a21e maps nlaps 1, 2, 3 to 6, 8, 10 and leaves any other
  value alone.
- CONFIRMED (0x1a171-0x1a183): with `v7edb0 != 0` the race is **3 laps and there is no lap menu** -
  that is the KID MODE path of `classes_menu`, so "engines locked" also fixes the race length
  (supports the `v7edb0` line in the names file). `twoplayer || netvoices` returns without touching
  `v88b9e`, `nlaps` or the rows (0x1a14d-0x1a162).
- CONFIRMED (0x1a1de-0x1a1e6, 0x1a264-0x1a2ac): outside splitscreen, TIME TRIAL (`v88bb2 == 4`) gets
  `nlaps = 6` with no menu; in splitscreen only the `v88b7e == 1` pass shows the menu, and after it
  a net game with `net88c14 != 0` drops out without testing `v88b9e` - the same shape as in
  `classes_menu`. Both passes carry their own copy of the switch tree, but the `nlaps = 6` store of
  the TIME TRIAL path is shared with its case 1 (`je 0x1a246`), which is what pinned the C shape
  (GOTCHAS, Watcom entry).
- CONFIRMED (rebuild `fn_1a2f8`, 103 bytes byte-identical at 0x1a2f8): three call sites in
  `fn_1f858` (0x1fd7a, 0x201fa, 0x20478) each call it immediately before `cars_menu`; it clears the
  eight ints at `v7d338` through the clib filler (`fn_430f7`, the dword path 0x39 into the `__stos`
  module at 0x430be) and then, when `netgame` with `v7ec9c == 1` or (non-net) `v7e574 != 0`, sets
  `v7d338[0]`, `[1]`, `[5]`, `[6]` to 1 (0x1a334-0x1a356). What those four flags mean is open
  (QUESTIONS.md).

## Traps

- `menu()`'s first argument is the row count, not an index: pass it short, and remember that this
  menu's count changes with `netgame` (4 vs 5) - a port that always passes 5 shows a blank row in
  net games.
- A Watcom switch's jump table sits *before* the function and its entries are
  not in body order: here 0x19da8 = [0x19e02, 0x19e37, 0x19e6f, 0x19dde] for
  cases 1,2,3,4, i.e. the source wrote TIME TRIAL first. Reading those four
  dwords is what pins the labels; taking the bodies in address order gives an
  off-by-one class mapping (it did, this session).
- `strcpy(cls_name, "")` is `mov al,[0x600ac]; mov [0x6aa40],al` - the pooled
  empty string, not a global (GOTCHAS).
- A `switch` on a `short` with cases 1,2,3 compiles to a balanced compare tree, not a jump table
  (`cmp ax,2 / jb / jbe / cmp ax,3 / je`, the global loaded once); writing the same thing as an
  `if / else if` chain re-loads the global for every test and costs 30+ bytes. Match the tree first,
  then the labels (this session: 58 instruction diffs as a chain, 1 as a switch).

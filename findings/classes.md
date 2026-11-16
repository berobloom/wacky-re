# Classes and game modes

Status: OPEN (`classes_menu` 0x19eac and the users of these globals are not rebuilt
yet; the values at `tbl7e730` are run-time)

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

## Traps

- A Watcom switch's jump table sits *before* the function and its entries are
  not in body order: here 0x19da8 = [0x19e02, 0x19e37, 0x19e6f, 0x19dde] for
  cases 1,2,3,4, i.e. the source wrote TIME TRIAL first. Reading those four
  dwords is what pins the labels; taking the bodies in address order gives an
  off-by-one class mapping (it did, this session).
- `strcpy(cls_name, "")` is `mov al,[0x600ac]; mov [0x6aa40],al` - the pooled
  empty string, not a global (GOTCHAS).

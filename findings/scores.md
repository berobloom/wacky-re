# Score / record tables

Status: OPEN (five helpers of this area are rebuilt - `fn_1cad0`, `fn_1c994`,
`enter_name` 0x1cb44, the table's owner `scores_screen` 0x1ce14 (near miss, 97
aligned instruction diffs) and the class record book `race_records_menu` 0x1d13c
(near miss, one diff block); the code that first fills `tbl77758` is not)

## Summary

Two 0x450-byte rows of records live at `0x77758` and `0x77ba8`; each row is 24
records of 0x2e (46) bytes, read through the pointer `v7d8a8`. `fn_1cad0`
(0x1cad0) selects a record into `v7d8a8`, `fn_1c994` (0x1c994) builds the
fixed-width stamp string that goes in the record's `+6` field, `enter_name`
0x1cb44 is the "PLEASE ENTER NAME.." screen, and `scores_screen` 0x1ce14 loads
the leader board, runs the name entry and inserts the run's score among the
first four records of the current mode's group. All of them sit between the
car-select chain and the name entry.

## Findings

- CONFIRMED (rebuild `src/ww.c`, `src/check.sh`: 115 bytes MATCH at 0x1cad0, its
  four address fixups checked against the game's bytes): `fn_1cad0(which, idx)`
  with arg1 = EAX and arg2 = EDX computes `idx * 0x2e` first (0x1cad0), then a
  switch over 1/2/3 (the compare chain 0x1cad3-0x1cadd) and stores
  `v7d8a8 = tbl77758 + (v88bb6 + v7ecb4) * 0x450 + {0, 0x170, 0x2e0} + idx * 0x2e`
  (0x1cb3b-0x1cb40); anything else returns without storing (0x1cadf). The three
  groups start 8 and 16 records into the row.
- CONFIRMED (disassembly 0x4316d-0x43179): `dos_gettime` 0x43164 is an
  `INT 21h/AH=2Ah` wrapper; it stores `[p] = dl` (1/100 s), `[p+1] = dh`
  (seconds), `[p+2..3] = cx` (CL = minutes, CH = hours) and `[p+4] = al`
  (daylight flag) of a 5-byte record. Its sibling `0x4317f` is the date call
  (`AH=2Ch`, month in `dh`) and is *not* what `fn_1c994` calls.
- CONFIRMED (rebuild `src/ww.c`, 312 of 313 bytes at 0x1c994, 4 instruction
  diffs): `fn_1c994(s)` appends to the NUL-terminated string `s`:
  `itoa(seconds_100ths)` padded to two digits (0x1c9bd-0x1c9d8), one space
  (0x600a0), one 4-byte entry of the month table at 0x10154, one space, and
  then the `itoa` of a value plus spaces until `strlen(s) >= 10`
  (0x1ca83-0x1cac1). The buffer is a caller's stack buffer (called at 0x1cf67
  with `lea eax,[esp+0x3c]`).
- CONFIRMED (0x10154 holds "JAN\0FEB\0MAR\0...DEC\0", 48 bytes; the copy at
  0x1c9b6 is `mov ecx,0xc` + `rep movsd`): the month table is a local
  `char mth[12][4]` in `fn_1c994`, not a pointer table.
- CONFIRMED (0x1c9bd reads `[esp+0x40]`, 0x1ca08 reads `[esp+0x41]`, 0x1ca75
  reads `WORD [esp+0x42]`, and 0x43164 fills those bytes as above): the value
  passed to the first `itoa` is the hundredths-of-a-second byte, the month index
  is `seconds - 1`, and the value of the second `itoa` is minutes and hours read
  as one little-endian word.
- LIKELY: the original meant a date and got a time, so the month field of the
  stamp is garbage unless the clock's seconds happen to be 1..12 (`mth` has 12
  entries and seconds run 0..59, so the index leaves the table from 13 up). Gap:
  nobody has run the original (STATUS Next 7); a save state plus a memory read of
  a record at 0x77758 would show what the field really contains.
- CONFIRMED (record copies at 0x1cfb4-0x1d01d and the insert at 0x1d03b-0x1d0b6, read in
  the `scores_screen` rebuild: words at +0, +2, +4 and inline `strcpy`s at +6 and +0x1a;
  46 = 6 + 20 + 20): a record is
  `short f00, f02, f04; char stamp[20] /*+6*/; char name[20] /*+0x1a*/` (an earlier
  version of this file had the two strings the other way round). Gap: the three
  words' meaning beyond row/player/score has not been tested.
- CONFIRMED (rebuild `src/ww.c` `scores_screen`, `src/check.sh`: 793 of the game's 807
  bytes, 97 aligned instruction diffs, all of them slot/register shape - see its
  `differs:` note): the insert writes `+0` = the row index `v88bb6 + v7ecb4`
  (0x1d04b-0x1d05d), `+2` = `myplayer` (0x1d060-0x1d06a), `+4` = the run's score
  `v7ed5c` (0x1d040-0x1d047), `+6` = the `fn_1c994` stamp (`strcpy` at 0x1d06e) and
  `+0x1a` = the `enter_name` name (0x1d08c).
- CONFIRMED (0x1ce87-0x1cf2d: three copies of one loop, reading through the
  displacements 0x7775c / 0x778cc / 0x77a3c = the three group bases plus 4): the insert works
  on the `+4` field of records **0..3** of the group picked by `v88bb2` (1/2/3; any other value
  leaves the local array untouched, 0x1ce95/0x1ce9e) - the group holds 8 records but only its
  first four take part.
- CONFIRMED (0x1cf2f-0x1cf57, 0x1cf77-0x1d02c): higher scores are better. The slot is the first
  `j` of 0..3 with `v7ed5c >= sc[j]`; `record[k] = record[k-1]` for k = 3 down to j+1 shifts the
  rest and the new record goes into `j`, so the table is best-first. If the score is lower than
  all four entries the game sets `sc[3] = v7ed5c - 1` first (0x1cf51-0x1cf57), which makes the run
  land in the last slot anyway: every run that reaches this function writes a record.
- CONFIRMED (0x1ce14's only call is 0x2d227, inside the end-of-race screen 0x2bff0, after the
  `joy88b40` / `splitscreen` / `[esp+0x338] == 1` tests at 0x2d204-0x2d221): the record book runs
  once, at the end of a race.
- CONFIRMED (0x3542c's prologue: `strcpy` of the string at 0x7c158, `itoa(arg)`, `strcat` of
  "WACKY" 0x61f9c and more): after the insert `scores_screen` calls `fn_3542c(v7ee5a)`
  (0x1d0c9-0x1d0d0) - SPECULATIVE: a per-slot score file name - and then `fn_1d704(v88bb2,
  v88bb6 + v7ecb4)` (0x1d119-0x1d12d), which loads the same "LEAD.PCX" 0x60dc4 behind it (0x1d71f)
  and walks records with `add v7d8a8, 0x2e` (0x1da3c): the table screen. LIKELY: `fn_3542c` reads
  or writes the saved table; nothing else in it has been read.
- CONFIRMED (0x1ce7d and 0x1d120 load `v88bb2` with `movsx`): `v88bb2` is a signed `short`;
  declaring it unsigned turns the 0x1d120 argument load into a `movzx`. `pal_a` behaves the same
  through `f375cd`'s EDX parameter (0x1ce57 vs 0x19508), but declaring `pal_a` `short` costs one
  of the 87 matches, so it stays unsigned in `src/ww.c`.
- CONFIRMED (only four code references to 0x77758 in the whole image,
  0x1caf8/0x1cb12/0x1cb31/0x1d870, and 0x77758 is zeros in the file; `scores_screen`'
  own reads go through the bases plus 4, 0x7775c/0x778cc/0x77a3c at 0x1cebf/0x1ceee/
  0x1cf1d): nothing in
  the game writes the table's address as a destination, so `tbl77758` is filled
  through a computed pointer or a segment base that the listing does not show.
  SPECULATIVE: that it comes from WACKY.DAT or from the demo runs. Test: boot the
  original and see whether the rows are non-zero before any race (captures/).
- CONFIRMED (walks at 0x1da3c `add v7d8a8, 0x2e`): the record pointer is stepped
  record by record elsewhere (0x1d8a8 writes `v7d8a8` the same way 0x1cad0 does,
  from 0x1d870).

- CONFIRMED (rebuild `src/ww.c` `enter_name`, `src/check.sh`: 718 bytes at
  0x1cb44, 21 aligned instruction diffs, all of them stack slots - see its
  `differs:` note): `enter_name(name, heading, n)` (arg1 = EAX the name buffer,
  arg2 = EDX the heading, arg3 = EBX the last index a character may go at) is the
  name-entry screen. It centred-prints `heading` at y 0xa with `wtext`
  (`center_x(strlen * 11)` inlined, 0x1cb74-0x1cba5), copies "PLEASE ENTER NAME.."
  (0x60d8c) into a local 0x28-byte buffer and prints it at (0xa, 0x60) (0x1cbbf-0x1cbce),
  then per key redraws: `pal_c = pal_a` + `setpalette`, the caret sprite `gn7eb2c`
  0xd x 0xf at (`cx`, 0x5b) with `cx = 0xaa + 8i` (0x1ccf0-0x1cd16), `wtext`, the
  prompt at (0xa, 0x60) and the name at (0xaa, 0x60), then `f37de0`, the
  `key88ccd` screenshot wait and the `pal_a` flip (0x1cd45-0x1cd87).
- CONFIRMED (0x1cd88-0x1cdfa): the key loop. `getch`; 0xd and 0x1b leave through
  `kbd_on` (0x1ce04); a character >= 'a' is folded to upper case at 0x1cda3 and
  every later test uses the folded byte; backspace (8) deletes only while `i > 0`,
  writes a space over the deleted slot and steps `cx` back (0x1cda6-0x1cdb9); the
  accepted set is A-Z, space and 0-9 (0x1cdbd-0x1cdd8) - anything else restarts the
  redraw without storing; a character is stored only while `i <= n` (0x1cde1, so
  indices 0..n inclusive) and `cx` steps forward only while `(short)cx <
  name_x + 8*(n+1)` (0x1cdef, 0x1cdfa).
- CONFIRMED (0x1cc16-0x1cc65): before reading keys the screen calls `f32cf8(v7d85c)`
  and `f278c4(0)`, then DOS SET VECTOR 9 (`int386x(0x21, &r, &r, &s)` with
  `r.x.eax = 0x2509`, `r.x.edx = oldkbd_off` 0x7d58c, `s.ds = oldkbd_seg` 0x7d5a4,
  `s.es = 0`), then `kbd_off`, then `pal_b = 0x7d00`. Same shape as `chat()`
  (findings/menus.md): the game puts the BIOS keyboard vector back. LIKELY: to hand
  the keyboard to `getch` for the length of the entry.
- CONFIRMED (0x1cc6e-0x1cc90; the caller at 0x1ce3f sets EBX = 0xb before the call
  at 0x1ce78 and passes the pooled empty string 0x600ac as the heading and a local
  buffer at esp+0x28 as the name - 0x14 bytes, the stamp array starts at esp+0x3c
  0x1cf5b): the name buffer is blanked with 12 spaces when
  `n <= 0xb` and 16 spaces when `n > 0xb` (literals 0x60db4, 0x60da0) - the field
  width is `n + 1`.

- CONFIRMED (rebuild `src/ww.c`, near miss at 0x1d13c; callers 0x2107e and 0x2108f in `fn_1f858`):
  `race_records_menu` 0x1d13c-0x1d33e is the **class record book** menu. The
  "drivers by class" menu (its items at 0x61400-0x61460: " AMATEUR CLASS DRIVERS", " PRO CLASS
  DRIVERS", " CHAMP CLASS DRIVERS", " TIME TRIAL DRIVERS", " LEAVE THIS MENU") stores its 1-based
  choice in `v88c80` (0x2103b); choice 4 calls 0x1d340 (the time-trial variant), any other choice
  calls 0x1d13c with EAX = the class (0x21088 `movsx eax, word [v88c80]`). Inside, four rows
  " BRONZE RACE     " / " SILVER RACE     " / " GOLD RACE       " / " LEAVE THIS MENU "
  (0x60dd8/0x60dec/0x60e00/0x604d0) go through `menu`
  (0x1d2e9) and a selection calls `fn_1d704(class, v7ecb4 + choice)` (0x1d321) - so the group of
  `tbl77758` is `v7ecb4 + choice`, `v7ecb4` being 0 regular / 3 bonus pack (`zones_menu` 0x16664).
  CONFIRMED: the item names are built by `strcat` on top of a prefix - `strcpy(row, " BONUS")` for
  the first three and `strcpy(row, "")` for the fourth when `v7ecb4 != 0`, `strcpy(row, "")` for all
  four otherwise (0x1d16f-0x1d1c7), then `strcat` of the row text (0x1d1cc-0x1d28b: the inline first
  `repne scasb`es the end of the destination), then `while (strlen(row3) < strlen(row0))
  strcat(row3, " ")` pads the last row to the first one's width (0x1d28e-0x1d2dc, the 1-char literal
  is 0x600a0). `v88bb0 = 0xd` and `v88c80 = <previous choice>` are re-set on every pass of the loop
  (the back edge 0x1d326 jumps to 0x1d156), and `v88c80` is saved on entry (0x1d147) and restored on
  exit (0x1d32b-0x1d331).
- STRONGLY SUPPORTED: 0x1d340-0x1d703 is the same screen for TIME TRIAL (` BRONZE TRIALS   `,
  ` SILVER TRIALS   `, ` GOLD TRIALS       `, 0x60e14/0x60e28/0x60e3c), and after the class choice a
  second menu of five generated rows - `mnames[0x3c*k] = <metal> + "TRACK " + itoa(1..5) + "  "`
  (0x1d53a-0x1d648, `itoa` 0x425f9 writing into the frame's first slot) plus " LEAVE THIS MENU " at
  0x7bd68 - run through `menu(6, v88bda, 0, 0)` at 0x1d671. Not rebuilt.

## Traps

- The 1-char menu-name padding literal 0x600a0 is `" "`, and the padding loop uses `strcat`, not
  `strcpy`: `strcpy(row, " ")` inside that loop would loop forever once `strlen(row0) > 1`. The
  Watcom inline gives it away - `repne scasb` over the destination before the copy is `strcat`
  (`strcpy` of a constant starts copying immediately).
- The four rows of these menus, 0x7bc3c / 0x7bc78 / 0x7bcb4 / 0x7bcf0, are `mnames[0]`, `[0x3c]`,
  `[0x78]`, `[0xb4]` - `0x7bc3c + 3 * 0x3c = 0x7bcf0`, four consecutive slots, which is how `menu()`
  reads them (`mnames[i * 0x3c]`). Writing the last row at `mnames[0xf0]` shows a blank fourth row.
- `v88c80` is both `menu()`'s initial cursor and the choice of the drivers-by-class menu: 0x2103b
  stores `menu`'s return there and 0x21088 reads it back as `race_records_menu`'s argument, while
  `race_records_menu` itself saves it (0x1d147) and rewrites it with the previous choice each pass
  (0x1d168). A port that treats it as one thing loses either the menu cursor or the class.
- The two 20-byte strings of a record are `+6` = the `fn_1c994` stamp and `+0x1a` = the typed
  name, the reverse of what their order in the record suggests: the insert writes the stamp into
  `+6` (0x1d06e) and the name into `+0x1a` (0x1d08c), while the shift copies `+0x1a` first
  (0x1cfd5) and `+6` second (0x1cff3). A port that reads the strings by field order swaps time
  and name.
- `scores_screen` lowers `sc[3]` to `score - 1` when the run beats none of the four entries
  (0x1cf51-0x1cf57), so the run is inserted anyway, in the last slot. A port that inserts only on
  a real beat never shows that name.
- The insert scans records 0..3 of the group but shifts `record[k] = record[k-1]` down to
  `k = j+1`: with `j = 0` the whole top four moves and record 3's contents are lost, while
  records 4..7 of the group are never touched.
- `enter_name`'s name buffer here is 0x14 bytes, not the 0x28 the frame seems to leave: the stamp
  array starts at `esp+0x3c` (0x1cf5b), 0x14 after the name at `esp+0x28` (0x1ce68).

- `enter_name` pads its caller's name buffer and writes indices 0..`n` inclusive
  (0x1cde1); a port that bounds the name by `n` alone drops the last character, and
  one that skips the pad keeps whatever the buffer held (the 0x1ce78 caller's buffer
  is an uninitialised 0x14-byte local).
- The `pal_b = 0x7d00` store at 0x1cc65 sits between `kbd_off` and the padding
  `strcpy`, easy to skip when that block is read as one "keyboard hand-off"
  sequence - it cost 7 phantom instruction diffs in the first rebuild.
- The row index of these tables is `v88bb6 + v7ecb4`, not either global alone
  (0x1cae5-0x1caf2); a port that uses one of them reads the wrong row.
- `fn_1cad0` returns without touching `v7d8a8` for `which` not in 1..3
  (0x1cadf) - the callers at 0x1cf9f/0x1cfaf call it twice and then read
  `v7d8a8` for both, so a stale pointer changes both records.
- `tools/wc/starts.py` lists 0x1c66c, 0x1c88c and 0x1c968 inside `fn_1c0f4`. Only 0x1c66c is not a
  start: it is the target of the `jbe` at 0x1c660 (and the byte before it ends a `jmp`-terminated
  block, which is how starts.py picks it up). 0x1c88c and 0x1c968 ARE functions - each has its own
  `push` prologue, its own `ret` (0x1c964, 0x1c993) and 4-byte padding - they are simply never
  called; see findings/menus.md, and `fn_1c0f4` ends at the `ret` at 0x1c88b (0x798 = 1944 bytes,
  not the 0x8a0 an earlier note measured by running on to 0x1c993, the next *named* function, past
  these two. Ghidra's `FUN_0001c0f4` stops with them; `tools/le/disasm.py --func` stops at the first
  `ret` and gets the end right too.

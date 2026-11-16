# Score / record tables

Status: OPEN (three helpers of this area are rebuilt - `fn_1cad0`, `fn_1c994` and
`enter_name` 0x1cb44; the table's owner function `0x1ce14` and the code that fills
`tbl77758` are not)

## Summary

Two 0x450-byte rows of records live at `0x77758` and `0x77ba8`; each row is 24
records of 0x2e (46) bytes, read through the pointer `v7d8a8`. `fn_1cad0`
(0x1cad0) selects a record into `v7d8a8`, `fn_1c994` (0x1c994) builds the
fixed-width stamp string that goes in the record's `+0x1a` field, and the
function at 0x1ce14 (not rebuilt) copies records around inside a row, and
`enter_name` 0x1cb44 is the "PLEASE ENTER NAME.." screen it calls before picking
the three best scores out of the row. Both helpers sit between the car-select
chain and the name entry.

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
- STRONGLY SUPPORTED (record copies at 0x1cfb4-0x1d01d: words at +0, +2, +4 and
  inline `strcpy`s from +6 and +0x1a; 46 = 6 + 20 + 20): a record is
  `short f00, f02, f04; char name[20] /*+6*/; char stamp[20] /*+0x1a*/`. Gap: the
  three words have never been read as anything but copies.
- CONFIRMED (only four code references to 0x77758 in the whole image,
  0x1caf8/0x1cb12/0x1cb31/0x1d870, and 0x77758 is zeros in the file): nothing in
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
  0x28-byte buffer as the name): the name buffer is blanked with 12 spaces when
  `n <= 0xb` and 16 spaces when `n > 0xb` (literals 0x60db4, 0x60da0) - the field
  width is `n + 1`.

## Traps

- `enter_name` pads its caller's name buffer and writes indices 0..`n` inclusive
  (0x1cde1); a port that bounds the name by `n` alone drops the last character, and
  one that skips the pad keeps whatever the buffer held (the 0x1ce78 caller's buffer
  is an uninitialised 0x28-byte local).
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

# Score / record tables

Status: OPEN (the two helpers of this area are rebuilt, the table's owner function
`0x1ce14` and the code that fills `tbl77758` are not)

## Summary

Two 0x450-byte rows of records live at `0x77758` and `0x77ba8`; each row is 24
records of 0x2e (46) bytes, read through the pointer `v7d8a8`. `fn_1cad0`
(0x1cad0) selects a record into `v7d8a8`, `fn_1c994` (0x1c994) builds the
fixed-width stamp string that goes in the record's `+0x1a` field, and the
function at 0x1ce14 (not rebuilt) copies records around inside a row. Both
helpers sit between the car-select chain and the pause/name-entry screen
(`0x1cb44`, which prints "PLEASE ENTER NAME..").

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

## Traps

- The row index of these tables is `v88bb6 + v7ecb4`, not either global alone
  (0x1cae5-0x1caf2); a port that uses one of them reads the wrong row.
- `fn_1cad0` returns without touching `v7d8a8` for `which` not in 1..3
  (0x1cadf) - the callers at 0x1cf9f/0x1cfaf call it twice and then read
  `v7d8a8` for both, so a stale pointer changes both records.
- `tools/wc/starts.py` lists 0x1c66c, 0x1c88c and 0x1c968 as candidate starts
  inside `fn_1c0f4`; they are not starts. Nothing calls them (no `call
  0x1c66c`/`0x1c88c`/`0x1c968` anywhere in the image) and 0x1c66c is the target
  of `jbe` at 0x1c660. STRONGLY SUPPORTED (Ghidra's `FUN_0001c0f4` + those
  greps): `fn_1c0f4` runs to 0x1c993, so it is 0x8a0 = 2208 bytes, not the 1396
  bytes the earlier STATUS note had.

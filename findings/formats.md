# File formats and archives on the disc

Status: **OPEN** (WACKY.DAT's container pinned below; the per-blob payload
formats are pinned only for PCX and GAM).

Method: parse `original/WACKY.DAT` offline (`tools/le/leparse.py`, plain
Python); cross-check every claim against the game's own access routines, all
byte-identical rebuilds (`datopen` 0x14128, `datfind` 0x1418c, `datclose`
0x14204, `seekdat` 0x14218, `telldat` 0x1422c, `datsize` 0x14238, `readdat`
0x1424c - `src/check.sh` MATCH lines). No live harness needed for this file.

## WACKY.DAT - the single data archive (CONFIRMED)

7 771 880 bytes (0x7696e8). `original/WACKY.DAT`, sha256 in
`original/SHA256SUMS`.

| Bytes | Content |
|---|---|
| 0-1 | word, entry count = 0x241 = 577 |
| 2 .. | 577 directory records of 22 bytes, then one blank record (22 bytes) |
| 0x31ae .. | the entry data, one after another, no gaps |

Record layout (`struct datent`, `src/ww.c:1223`, and `datopen` reads exactly
`n * 22` bytes into `datdir` 0x65080):

    char name[14];   /* upper case, zero padded */
    long size;       /* LE */
    long offset;     /* LE, relative to file offset 2 */

- `datfind` 0x1418c `strupr`s the name, scans `datdir` from index 0, stops at
  the first record whose `name[0] == 0` (the terminator), sets `datidx`
  0x88b00 and, when found, `seekdat(datdir[datidx].offset + 2)`
  (`src/ww.c:2408-2430`). Return value 1/0 = found, and that is what
  `v88bd6` 0x88bd6 holds: every write to it is `mov [0x88bd6], ax` straight
  after a `datfind` call (`tools/le/xref.py ww 0x88bd6`: 0x17e60, 0x2149d,
  0x27c9b, ...), and the callers test `if (v88bd6 == 0 && v88bea != 0)`
  (0x12135, 0x12448, ...). STATUS iter 47's "`v88bd6` = `datfind`'s index" is
  wrong - the index is `datidx` 0x88b00.
- The offsets are relative to 2, not absolute (CONFIRMED, two independent
  checks): the directory with its terminator ends at 2 + 578*22 = 0x31ae, and
  entry 0's data begins exactly there (`offset` 0x31ac + 2); walking all 577
  records in order gives no gap and the last one, `WWREG.BIN` (size 0xfa00,
  offset 0x768746), ends at 0x7696e8 = the file size. With absolute offsets the
  last record would end 2 bytes short and would overlap the terminator.
- The terminator slot is not zeroed: 0x3198-0x31ae holds `00 'WREG.BIN'` plus
  zeros, the leftovers of a record that was edited out. `datfind` never reads
  past it because `name[0] == 0`. (CONFIRMED from the bytes; the "edited out"
  reading is SPECULATIVE.)

## Entry census (CONFIRMED: the parsed set of 577 names)

42 records each of `.GAM`, `.RD`, `.M`, `.PAR`, `.POS`, `.SIN`, `.SPW`
named `1..42` (`1.GAM` .. `42.GAM`, each set contiguous 1-42, no duplicates) -
294 per-track files, so the shipped game has 42 tracks. Plus 116 `.PCX`, 67 `.SP`, 36
`.VOC`, 16 `.KLM`, 16 `.MID`, 7 `.INF`, 6 `.BMC`, 2 each of `.BIN`
(`WWSW.BIN`, `WWREG.BIN`), `.FX` (`AUDIOHED.FX`, `AUDIOT.FX`), `.HTT`, `.OTT`,
`.TAB`, and one each `.ATR`, `.DAT` (`TRIG.DAT`), `.ING` (`WACKY.ING`), `.MOV`,
`.SDX` (`WACKY.SDX`), `.XY` (`MAP.XY`), and three records with no dot: `VIEW`
(1280), `NDIST` (89600), `SLP` (7680). Sum = 577 = the header word.

Both `42.GAM` (159 bytes) and `20.GAM` (150 bytes) are present, so this copy is
the one `release_check` 0x34458 calls fully upgraded (`v7eca0` = 3) - see the
release rule in `src/ww.c:18497` (findings/game-loop.md for its screen).

## PCX blobs (CONFIRMED)

A `.PCX` entry is one whole PCX: `pcx_load` 0x27060 does `fread(hdr, 0x80, 1)`
and then reads `datdir[datidx].size - 0x380` bytes into `p6a978[n]`
(`src/ww.c:12076-12087`), i.e. 0x80-byte header + data + a trailing 0x300-byte
palette. Checked in the file for `APOG1.PCX` (0x73ae), `CHECK.PCX` (0x2596) and
`BSBCS.PCX` (0x9224): first byte 0x0a (PCX magic), and the byte just before the
last 0x300 is 0x0c, the standard PCX palette selector. `pcxfile_load` 0x270dc
applies the same `- 0x380` to a loose `.PCX` file (its size limit is 0xfa00).
The 116 `.PCX` entries include the title/logo pages and the menus' one-file
pictures, e.g. `BSBCS.PCX` / `SBCS.PCX` picked by `fn_193a8` 0x193a8
(findings/menus.md).

## .GAM - per track text (CONFIRMED it is text; fields OPEN)

CRLF-separated lines, 150-171 bytes. `1.GAM` starts

    f1.pcx / f2.pcx / ma1.pcx / ma2.pcx / back1.pcx / 1250 / 1500 / 1 ...

which matches what `track_load` 0x279e8 parses (four names, then the fifth,
then numbers; `src/ww.c:12113`). Numbers per line, no other text. `20.GAM` and
`42.GAM` have the same shape and one line fewer or more; the field list is not
pinned (the extra trailing lines in `42.GAM` - `1 2 4 3` - are unread).

## WACKY.ING / WACKY.CFG - the config block (layout CONFIRMED)

`main` 0x35ca4 (0x35e15: `mov eax, 0x6201c`, the literal at 0x6201c) does
`datfind("WACKY.ING")` + `fread(cfg7ee1c 0x7ee1c, 0x492, 1, datfile)` (0x35e29);
then it requires `WACKY.CFG` to be exactly **0xada** bytes (`cmp edi, 0xada`
0x36020) and reads it as `cfg7ee1c` 0x492 + `cfg7b6d8` 0x18 + `cfg77128` 0x630
(freads 0x36041 / 0x36057 / 0x3606d), and requires `word [0x77430] == 0xfa`
(0x36072; `w77430`, and the shipped `original/WACKY.CFG` has 0xfa there).
0x492 + 0x18 + 0x630 = 0xada = 2778 = that file's size, and the writer does the
same three transfers in the same order (`save_config` 0x11a38 **MATCH**, whose
out-of-line write block is 0x11967-0x119d4, entered by `jne 0x11967` from
0x11a54) - CONFIRMED. Version string
"1.1" at +2 (bytes; that it is the version is LIKELY, `release_check` prints
"WACKY WHEELS VERSION 1.1" separately).

### the 0x492 block = a 0x42-byte header + 24 records of 0x2e (CONFIRMED: 0x42 + 24 * 0x2e = 0x492)

The records have the shape already pinned for `tbl77758` (findings/scores.md):
`short f00, f02, f04; char stamp[20] /*+6*/; char name[20] /*+0x1a*/`. The two
strings sit 0x14 apart (stamps at +0x48, 0x76, ... 0x46a; names at +0x5c, 0x8a,
... 0x47e) and the record starts 0x2e apart, the last ending exactly at 0x492.
**The "0x44 stride" of iter 49's note is wrong** - it mis-counted those pairs;
0x2e is the same stride `fn_1cad0` walks `tbl77758` with.

All 24 names are `"KNOCK ME OFF!"`, all 24 stamps `"MAR 1994"`, all 24 `f04 = 0`,
and (f00, f02) = group 1 slots 0-7, group 2 slots 4,5,6,7,0,1,2,3, group 3 slots
7,0,1,2,3,4,5,6. LIKELY: `f00` is the class group and `f02` the slot inside it -
three groups of eight is exactly how `fn_1cad0` splits a row (`{0, 0x170, 0x2e0}`
= 8 and 16 records). Why each group starts at slot 0 / 4 / 7: OPEN
(QUESTIONS.md Q11).

CONFIRMED (`tools/le/rangexref.py ww 0x7ee1c 0x7f2ae` - every decoded
instruction with an operand inside the range; 17 distinct addresses, **none in
+0x42..+0x491**): no code reads or writes one of the 24 records field-wise; they
only round-trip through the whole-block `fread`s (0x35e29, 0x36030) and
`fwrite` (0x1197f). Caveat: the census sees literal operands, and those three
are the only instructions that materialise the block's base address at all.

Header fields (offsets from 0x7ee1c; ING / CFG values from the two byte images):

| Offset | Name | Where | ING | CFG |
|---|---|---|---|---|
| +0x02 | `"1.1\0"` | never read | "1.1" | zeroed |
| +0x0a | dword | never read | 15476 | 0 |
| +0x0e | `cfg7ee2a` | tested 0x1196c, cleared 0x11976 | 1 | 1 |
| +0x14 | dword -> `v7eba0` | 0x35e43 | 14666 | 0 |
| +0x18 | dword -> `v7eba4` | 0x35e39 | 51456 | 0 |
| +0x1c..+0x26 | six words -> `v88b5c, v88b5e, v88b52, v88b3c, v88b3e, v88b48` | `ctl_setup` 0x14b00, 0x14b34-0x14b82 (`src/ww.c:3015`) | 0 | 0 |
| +0x28, +0x2a, +0x2e, +0x30, +0x32 | five flags `0x7ee44/46/4a/4c/4e` | written by the startup helper 0x11c9c (0x11cc8-0x11f79) | 0,1,0,0,0 | same |
| +0x34, +0x36, +0x38, +0x3a | 1, 1, 7, 544 | never read | as shown | as shown |
| +0x3c | `v7ee58` | 0x1e461 (`demo_mode`), 0x3680c (`main`) | 2 | 1 |
| +0x3e | `v7ee5a` - the save slot | 0x1d0c9 -> `scores_write(v7ee5a)`, 0x31b26 | 3 | 1 |
| +0x40 | `v7ee5c` | never read | 3 | 1 |

`v7eba0`/`v7eba4` are `malloc` sizes: `v7ebb8 += v7eba4; A_v7eb20 = malloc(v7eba4)`
whose failure logs "ingame sprites failed", and `v7ebb8 += v7eba0; A_v7d820 =
malloc(v7eba0)` which logs "effects Failed" (`src/ww.c:18237-18250`; the sizes
reaching `malloc` is CONFIRMED, the buffer purposes are LIKELY from those two
logerr strings, and `v7ebb8`/`v7eb88` look like a bytes-allocated counter).

CONFIRMED (byte compare `original/WACKY.CFG` against the DAT entry `WACKY.ING`):
560 of the first 0x492 bytes differ - the CFG's version field, +0x0a, +0x14,
+0x18 and **all 24 records** are zeroed, `cfg7ee2a` stays 1, and +0x3c/+0x3e/+0x40
are 1/1/1 against the ING's 2/3/3. `v7ee5a` = 1 means this config selects save
slot 1, so the shipped `WACKY1.HI`/`WACKY1.TIM` are its files
(findings/scores.md iter-50 row). LIKELY: `SETUP.EXE` writes those zeros - it is
the program the game's error texts name; SPECULATIVE: that it is also what
clears the 24 default records.

### the other two config blocks (bytes CONFIRMED, meaning OPEN)

- `cfg7b6d8` 0x18 = 12 words; in the shipped CFG they are BIOS scancodes
  `4b 4d 38 1d 50 39 20 21 1e 1f 22 23` (Left/Right/Down cursor, Alt, Ctrl,
  space, then D F A S G H). `main` stores its address into `v7d8a4` (0x35d57),
  `v7d8ac` (0x35d39), `v7d990` (0x35d4d) and `v7d8b0 = 0x7b6e4` = base+0xc
  (0x35d43); `cars_menu` 0x1a380 tests bytes of
  it through an index (`cmp byte [eax + 0x7b6d9], 0` 0x1abc2). LIKELY the
  player key map (the first six match the control names stored in `cfg77128`,
  see below); which slot is which player: OPEN.
- `cfg77128` 0x630: 56 distinct addresses inside it are touched
  (`rangexref.py ww 0x77128 0x77758`); `ctl_setup` 0x14b00 reads +0x08..+0x12
  (0x14ba6-0x14bf4), `main` tests +0x308 (`w77430 == 0xfa`, 0x36072), and the
  control names are strings inside it - the CFG bytes at 0x77432 read "LEFT
  CURSOR", "RIGHT CURSOR", "ALT", "CTRL", "DOWN CURSOR", "SPACE", "D", ...
  (findings/menus.md has the 84-byte form used by the F1 controls screen).
  It ends at 0x77128 + 0x630 = 0x77758, the address of `tbl77758`.

## Other disc files

| File | Size | What is known |
|---|---|---|
| `WACKY.DTT` | 96 | Duck Shoot hall of fame: 4 records of 0x18 = `char name[12]` (space padded), a zero dword, a dword score. Bytes read: SHAUN 16, ANDY 12, NUGGET 8, RUEBEN 6 - exactly the defaults `duckfame_load` 0x35534 writes when the file is missing, and `duckfame_write` 0x35718 writes the same shape (CONFIRMED, both byte-identical rebuilds; literal `WACKY.DTT` at 0x61fb0, used at 0x355a6 and 0x3574a). |
| `WACKY.CFG` | 2778 | 0xada = the config block `cfg7ee1c` 0x492 + `cfg7b6d8` 0x18 + `cfg77128` 0x630, read at 0x36041/0x36057/0x3606d and written by `save_config` 0x11a38 (MATCH); a wrong size or `w77430 != 0xfa` means "Incorrect Wacky Wheels Config File!" (`src/ww.c:19288-19316`, `main` is a near miss but this block is straight-line). The `WACKY.CFG` literal 0x6208a, the "WACKY.CFG will be written here." text 0x62065. This copy's date is 2018, so it is a runtime artifact of an earlier run, not shipped data (LIKELY, from the mtime alone); its 0x492 block differs from the shipped `WACKY.ING` in 560 bytes - version, sizes and all 24 default records zeroed, save slot 1 (see above). |
| `WACKY1.HI` | 6624 | Score book of save slot n = `v7ee5a`: **six rows of 0x450**, loaded at `tbl77758 + 0x450` (0x35b4d) by `records_load` 0x357ac and rewritten by `scores_write` 0x3542c (0x35512) - both byte-identical rebuilds, so the name construction is pinned: `savedir` 0x7c158 (0x357bc) + `"WACKY"` 0x61f9c + `itoa(n,10)` + `".HI"` 0x61fac, which is the tail of the pooled literal `"UPG.HI"` 0x61fa9 (byte dump of the pool 0x61f90-0x61fcf: `"WACKY"`, `".TIM"` 0x61fa4, `"UPG.HI"`, `"WACKY.DTT"` 0x61fb0, `" YWAC"`, `".OTT"` 0x61fbf, `"T P.HTT"` 0x61fc4). The shipped copy is byte-identical to the DAT entry `WAC1.HTT` (6624 bytes at file offset 7750632; `WAC2.HTT` 7757256 equals it too), 144 filler records - name "KNOCK ME OFF!", stamp "01 SEP 1994" (findings/scores.md). |
| `WACKY1.TIM` | 3600 | The same slot's time-trial book: `trial70730` = 30 * 0x78 (`struct trialrec`, 0xe10) via `records_load` (`.TIM` 0x61fa4, the DAT fallback `WAC<n>.OTT` at 7743432/7747032) and `trial_write` 0x35324; 0xe10 = 30 * 0x78 and the shipped bytes are `struct trialrec` field for
field (`src/ww.c:15797`: four 20-byte lap names at +0/+0x14/+0x28/+0x3c, the four lap times as
+dwords at +0x50, the race name at +0x60, the race time at +0x74 - CONFIRMED from a byte dump). The
shipped copy is byte-identical to `WAC1.OTT`; its records are real names with dword times (BRIAN
DHATT, CHRIST WHITE, ...). |

## Traps

- `datdir`'s `offset` field is relative to 2, not to 0: reading it as an
  absolute file position moves every blob 2 bytes and makes the PCX headers
  look wrong (the magic byte would be the palette selector of the previous
  blob). The `+ 2` in `seekdat(datdir[datidx].offset + 2)` is the check.
- The directory is blank-terminated, not count-terminated: `datfind` ignores
  the header word once it is loaded, and the terminator slot is dirty. A parser
  that reads `count` records and then expects data at `2 + count*22` lands 22
  bytes early.
- 42 tracks ≠ 42 `.GAM` "records": `42.GAM` is the file of track 42. Its
  presence in the archive - not its contents - is what `release_check` tests.
- The 24 record slots inside `WACKY.ING` are **inert** (range census: nothing
  touches +0x42..+0x491). Seeding `tbl77758` from them is wrong - the table's
  defaults come from the DAT entries `WAC<n>.HTT`/`WAC<n>.OTT` through
  `records_load` 0x357ac (MATCH).
- Take `v7eba0`/`v7eba4` from the ING block *before* reading `WACKY.CFG`: the
  shipped CFG has 0 in those dwords, so a port that copies them after the CFG
  read calls `malloc(0)` twice (0x35e39/0x35e43 precede 0x36030).
- `xref.py ww 0x7ee1c` alone does not tell you what a config block holds:
  Watcom reaches each field through its own absolute address (`cfg7ee1c + 0x3e`
  is `word [0x7ee5a]` = `v7ee5a`). Census the range with
  `tools/le/rangexref.py` before concluding a field is unused.

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

## WACKY.ING - the shipped default config (partly CONFIRMED)

Entry `WACKY.ING`, 0x492 bytes. `main` 0x35ca4 (0x35e15: `mov eax, 0x6201c`,
the literal at 0x6201c) does `datfind("WACKY.ING")` + `fread(cfg7ee1c, 0x492,
1, datfile)` (`src/ww.c:19262`); other code takes `((short *)cfg7ee1c)[14..19]`
into `v88b5c, v88b5e, v88b52, v88b3c, v88b3e, v88b48` (`src/ww.c:3015`), and
`main` takes the dwords at +0x14/+0x18 into `v7eba0`/`v7eba4`.
First bytes (from the archive): `00 00` `"1.1"` padding, then dwords
0x3c74, 1, 0x394a, 0xc900 ...; from +0x30 the same pair of strings recurs at a
0x44 stride - `"MAR 1994"` and `"KNOCK ME OFF!"`. So: version string "1.1" at
+2 (CONFIRMED as bytes; that it is the version is LIKELY, `release_check`
prints "WACKY WHEELS VERSION 1.1" separately), and 0x44-byte records after a
14-byte header (LIKELY from the stride, the field meaning OPEN).

## Other disc files

| File | Size | What is known |
|---|---|---|
| `WACKY.DTT` | 96 | Duck Shoot hall of fame: 4 records of 0x18 = `char name[12]` (space padded), a zero dword, a dword score. Bytes read: SHAUN 16, ANDY 12, NUGGET 8, RUEBEN 6 - exactly the defaults `duckfame_load` 0x35534 writes when the file is missing, and `duckfame_write` 0x35718 writes the same shape (CONFIRMED, both byte-identical rebuilds; literal `WACKY.DTT` at 0x61fb0, used at 0x355a6 and 0x3574a). |
| `WACKY.CFG` | 2778 | The config the game writes (`WACKY.CFG` literal 0x6208a, the "WACKY.CFG will be written here." text 0x62065); this copy's date is 2018, so it is a runtime artifact of an earlier run, not shipped data (LIKELY, from the mtime alone). |
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

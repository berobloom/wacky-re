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

## .GAM - per track text (CONFIRMED: 25 or 28 CRLF-separated lines)

`gam_load` 0x2717c (byte-identical rebuild, `src/ww.c:12126-12180`) reads one
value per line: four picture names into the 14-byte slots `gam7eabc[0..1]` and
`gam7eaa0[0..1]`, a fifth name into `gam6a4e0`, two numbers (the start point
`v88b0a`/`v88b0e`, each `+ 0x400`), one line nobody reads (line 8, `1` in every
file), eight pairs of numbers (the start-grid slots `order_a[0..7]` /
`order_b[0..7]`, each `+ 0x3e0`), then `v88b60`; if that is non-zero, three more
numbers (`v88b5a`, `v88b4e`, `v88b4c`). CONFIRMED by the line census over all 42
files: 36 have exactly 25 lines, 6 have 28, `v88b60` is 0 or 1 and 28 lines
occurs exactly when it is 1 (`1.GAM` 162 bytes/25 lines, `16.GAM` and `42.GAM`
160/159 bytes/28 lines).

The four picture names get an `A_` prefix (`src/ww.c:12143-12151`) and every
prefixed name exists in WACKY.DAT (census over all 42: `1.GAM` line 1 `f1.pcx`
-> entry `A_F1.PCX`), as do the fifth names, which are only `back1.pcx`,
`back2.pcx`, `back3.pcx`; `datfind` 0x1418c uppercases its argument (findings
note in `analysis/ww_names.txt:256`), which is why the mixed case works.
`str7d784` = `gam7eabc[0]` (whose trailing 0x300 bytes are the track's palette,
`src/ww.c:12440-12444`), `gam6a4e0` is `pcx_load`ed as screen 1 (0x27de0), and
the four `A_` names are the tile and mask pictures painted into `v7d75c` by
`track_pics_load` 0x277b8 - see `.M` below.

## The per-track payloads: `.POS`, `.SIN`, `.M`, `.PAR`, `.RD`, `.SPW`

Sizes are constant per extension over all 42 tracks (census of every entry with
`tools/le/datdump.py`): `.SIN` 1296, `.POS` 8194, `.PAR` 25600, `.M` 4096; `.RD`
and `.SPW` are count-prefixed and vary. The blobs are shared between track
slots: all 42 `.M` differ, but there are only 30 distinct `.POS` (12 of them -
tracks 16-21 and 37-42, never loaded - are the same all-zero map), 18 `.SIN`, 9 `.PAR` and 29
`.GAM`. The track slot is therefore not the same thing as the track art.

### .POS - the 64x64 zone map (CONFIRMED)

`track_load` does `readdat_in(map, 0x2000); readdat_in(&ncells, 2);`
(`src/ww.c:12450-12451`), so the 8194 bytes are a 64x64 array of shorts (8192)
plus one word. `map_at` (rebuild `src/ww.c:1513-1517`) reads it as
`map[((y - 0x400) >> 5) * 64 + ((x - 0x400) >> 5)]` and returns 0 outside
0..0x7ff, so the array covers the 2048x2048 world in 32-unit cells and a cell
value is a zone class. `ncells` is the highest class in the map: for all 42
files max(map) == `ncells` and the number of distinct values is `ncells` + 1
(census: 26..71 for tracks 1-15 and 22-36; tracks 16-21 and 37-42 have
`ncells` = 0 and an all-zero map while their `.M`/`.SIN`/`.PAR` are real).
`v88ba0 = ncells / 4` (`shr` at 0x27dc7, `src/ww.c:12452`).

### .SIN - 108 records of 12 bytes, one per tile: the surface classes (CONFIRMED)

`track_load` loops 0x6c times doing `readdat_in(&skip, 4); readdat_in(sp, 8);`
(`src/ww.c:12458-12462`): stride 12 in the file (108 * 12 = 1296 = the size),
stride 8 in memory (`sin7ad88[0x6c][8]` at 0x7ad88), and the first 4 bytes of
every record are read and thrown away.

The array is written once (0x27dbc -> `v7d870 0x7d870`) and read once - by the
asm `surface_at` 0x37b04 (`tools/le/xref.py ww 0x7d870` finds exactly those two
sites). It takes the tile under a point from the tile map (`mov edx,[v7d6a4]`,
`shr eax,5; shl eax,9`, `shr ecx,5; shl ecx,2` 0x37b5c-0x37b72 = the dword at
row `[y>>5]`, column `[x>>5]` of a 0x80-dword map), converts the tile pointer
to a tile index (`sub eax,[v7d64c]`, `shr eax,0xa`, `shl eax,3` 0x37b74-0x37b7c
- so a tile is 0x400 bytes) and indexes `v7d870 + index * 8` (0x37b80-0x37b86).
Then it reads one byte of the tile's **mask** half, `tile + 0x1b000 +
(y&31)*32 + (x&31)` (0x37b8d-0x37b95): if it is 0 `v7e568` gets the record's
first stored dword, otherwise the second (0x37b9e / 0x37ba9). So the file's
`.SIN[12*i+8]` = the surface class over the transparent part of tile i and
`.SIN[12*i+12]` = the class over its solid part. `v7e568` indexes the 0x12e-byte
`surfs[]` records (src/ww.c:1425), and the two dwords run 0..27 across all 42
files (24 and 23 distinct values) while `WACKY.SDX`'s count word is 30, 29
records read after `v88b7c--` (`src/ww.c:12592-12593`) - LIKELY the surface
index; CONFIRMED that these dwords are what `surface_at` stores into `v7e568`.

The discarded leading 4 bytes are 0x1, 0x10000 or 0x10001 (two shorts, each 0 or
1; census over all 42: 1544 / 476 / 2516 records). Nothing in WW.EXE reads them
(the only reader of the array uses the two dwords at +4 and +8 of the memory
record, which is the file's +8 and +12): meaning OPEN.

### .M - 64x64 tile indices (CONFIRMED)

4096 bytes. `track_load` reads one byte per column for rows 0x20..0x5f of the
dword map at `v7d6a4` and stores `v7d75c + (byte << 10)` per column
(`src/ww.c:12471-12479`); `v7d648 = v7d644 = 0x80` (`src/ww.c:17557-17558`) so
the map is 128x128 dwords and the file fills its centre 64x64. Observed byte
range over all 42 files: 1..107, i.e. 108 tiles. A tile is 0x400 bytes = 32x32
8-bit pixels (`>> 0xa` at 0x37b7a above, and the pixel address
`(y&31)*32 + (x&31)` at 0x37b4d-0x37b5a).

The tile *graphics* are not in `.M`: `track_pics_load` 0x277b8 `pcx_load`s the
four `A_` names from the `.GAM` and the asm `pcx_tiles` 0x376d7 paints them into
`v7d75c` in four quarters `q = v7d604 / 4` apart (`src/ww.c:12288-12305`,
`v7d604 = 0x36000` at `src/ww.c:18127`): `gam7eabc[0..1]` at +0 and +0xd800,
`gam7eaa0[0..1]` at +0x1b000 and +0x28800. 0x36000 / 0x400 = 216 tiles = 108
picture tiles plus 108 mask tiles, which is exactly the `.SIN` record count and
the +0x1b000 `surface_at` adds. CONFIRMED for the addresses; what `pcx_tiles` does
inside a quarter is asm and unpinned (OPEN).

### .PAR - 25600 bytes of raw picture, blitted to A000:7d00 (transfer CONFIRMED)

(The external pc-wackywheels-doc `docs/03-tracks.md` reads it as 640x40 by row smoothness alone;
the copier and the live screenshot fit below make it 1280x20 - do not take that over.)

Read raw (no PCX header) into `p6a980` (`src/ww.c:4415`); when `v7d998` is 0 or
3 - not in the record/playback modes 1 and 2 (`v7d998` is set to 2 or 3 at
0x1e450-0x1e485, to 1 by `demo_start`) - `v7eb54 = p6a980` and the asm
`horizon_upload` 0x36f55 streams it (disasm):

    0x36f5e mov edi,0xa0000 ; 0x36f63 add di,0x7d00   ; A000:7d00
    0x36f68 mov cx,0x6400                             ; 25600 iterations
    0x36f6c mov dx,0x3c4 ; 0x36f70 mov ah,1 ; 0x36f72 mov al,2 ; 0x36f74 out dx,ax
    0x36f76 lodsb ; 0x36f77 mov [edi],al              ; one byte per plane
    0x36f79 shl ah,1 ; cmp ah,8 ; jle loop ; 0x36f84 inc di ; ah=1

The sequencer map-mask register (port 0x3c4 index 2) is re-written on every
iteration with a value rotated by `shl ah,1`, and DI advances only when the
rotation passes 8, so the mask sequence is 1, 2, 4, 8: each of the 6400 video
addresses A000:7d00..0xe0ff is written four times, once per VGA plane, i.e. the
blob stores **4 bytes per video address** (CONFIRMED from the loop shape). 0x7d00
= 32000 = the start of the third 0x3e80-byte page.

**Pixel geometry: a 1280x20 8-bit panorama, row-major (`pixel(x, y) = blob[1280*y + x]`)** -
CONFIRMED (iter 56c, live harness on track 22 plus the copier's disasm):

- The four writes per address land in planes 0, 1, 2, 3 (Mode X, unchained): a
  CPU read of A000:7d00..e0ff in the race returned one plane, and it equals
  `22.PAR[1::4]` in all 6400 bytes. So blob byte `4a + k` is plane `k` at address
  `a`, i.e. pixel `4a + k` of the off-screen area.
- The copier (asm, 0x395dd-0x396c8, in the race branch `v7d998 != 2`) copies 20
  rows of 0x50 bytes, adding 0xf0 to the source after each (a 320-byte = 1280-pixel
  source row), from `v7d860` (the scroll window, a pointer into A000:7d00..) to
  `0xa0000 + 0x2260 + pal_a` = screen rows 110..129 of the page being drawn, with
  `rep movsb` (the latch copy moves 4 pixels a byte). So the screen shows a
  320-pixel window of a 1280-pixel horizon strip: the 20 rows directly above the
  horizon, scrolled with the view.
- Live check: with `v7d860` = 0xa7d2a (window 42 addresses = 168 pixels in), the
  screenshot's rows 110..129 against `22.PAR` as 1280x20 from x = 168 give one
  colour per index for 93.9% of the 6400 pixels (karts and HUD cover the rest);
  rows 108..112 and other offsets score 0.83 or less. 22.PAR is a sunset sky
  gradient (indices 235..254) with the sun's disc.
- `v7d998 == 2` (demo playback) instead copies 0x28a0 bytes in one block (0x39604).
- The old statistic (mean |Δ| smallest at lag 640) only says the strip is smooth
  over half its width; the earlier LIKELY "4 bytes per pixel, 320x20" was wrong.

### .RD - the track line segments (CONFIRMED)

A word count then that many 14-byte records; `(size - 2) / 14` equals the count
word in all 42 files (census, no exceptions) - 22..83 lines. `track_load` reads
`v88b7a` and then `readdat_in(&rd7ee0c, 0xe)` per line and adds 0x400 to the
four coordinates, so the record is `short x0, y0, x1, y1, f08, f0a, f0c` with
the coordinates in the 0x400-based world (`src/ww.c:12485-12502`,
`struct rdrec` at `src/ww.c:12375`).

The three tail fields, by census over all 2177 records of the 42 files (the hypotheses come from
the external pc-wackywheels-doc `docs/03-tracks.md`; tested here, the code that reads them not yet
read):
- `f0c` = `round(hypot(x1-x0, y1-y0))` in all 2177: the segment length. STRONGLY SUPPORTED (census).
- `f08` = the heading `atan2(y1-y0, x1-x0)` in units of 1/1920 turn, 0 = +x: within 1 unit in 2001,
  at most 5.1 units off in all 2177 (a table-based atan, LIKELY; TRIG.DAT is 15360 bytes = 1920 x 8).
  STRONGLY SUPPORTED (census).
- `f0a` takes the values 0..7 (366/165/330/214/358/243/305/196), not "0 or 1" as the external doc
  says, and is not the heading's octant (`f08 / 240` and `(f08 + 120) / 240` match 0 records).
  Meaning OPEN.

### .SPW - the entity list (CONFIRMED)

A word count then that many 12-byte records; `(size - 2) / 12` equals the count
word in all 42 files (7..38 entities). `ents_load` 0x21490 does
`fread(&nents, 2, 1); fread(ents, nents * 12, 1)` and then `x += 0x400`,
`y += 0x400` (`src/ww.c:9051-9067`), so the record matches `struct ent`
(`short id, f2, x, y, f8, frame`, `src/ww.c:2640`) with `f2` cleared on load and
`id` naming the sprite set in `SPRITE.ATR` (`ent_sprites_load` 0x21310,
`src/ww.c:8248-8252`). CONFIRMED by census: the `id` fields over all 42 files
span exactly 0..35 and `SPRITE.ATR` is 1442 = 2 + 36 * 40 bytes, i.e. the 36
`struct attr` records `sprites_load` reads (`src/ww.c:2326-2327`) - `frame`
counts frames of `w * h` bytes inside that record (`src/ww.c:2731-2745`). The
`f8` field is not read there: OPEN.

### SPRITE.ATR - the 36 entity sprite sets (CONFIRMED layout)

1442 bytes = a count word (36) plus 36 records of 40 bytes. `sprites_load`
0x13a74 reads the word into `nsprattr` 0x88b82 (0x13e96) and then
`readdat(sprattr 0x72998, nsprattr * 40)` - the game's size math is
`lea eax,[edx*4]; add edx,eax; lea edx,[edx*8]` = n*40 (0x13eb2-0x13ec6), so the
record size is CONFIRMED, not inferred. Dump: `tools/le/datdump.py SPRITE.ATR`.

Record layout (every field the game READS is listed with its site; a complete
scan of the code object finds reads of exactly these seven offsets -
`grep -oE '\[[a-z0-9]+ \+ 0x729..\]'` over `tools/le/disasm.py ww` output over
0x10260-0x36a60 gives 0x72998/0x7299c/0x7299e/0x729a4/0x729a6/0x729bc plus the
name at +0x10 via `add eax,0x10` 0x2137c):

| Offset | Field | Evidence |
|---|---|---|
| 0x00 | `short kind` | `cmp word [ebp+0x72998],2` 0x2139d (first kind-2 id -> `v7eda4`), `movsx ... ,2`/`jl` 0x21439/`jle` 0x21454 in `ents_flags` 0x213f0, 0x2322d, and the hit tests (`src/ww.c:9038,9043,10121`) |
| 0x02 | unused, 10 in all 36 records | no read anywhere (offset scan above) |
| 0x04 | `short w` | `movsx edx,[ebp+0x7299c]` 0x2134f |
| 0x06 | `short h` | `movsx eax,[ebp+0x7299e]` 0x21356 |
| 0x08, 0x0a | unused, always a copy of w,h | no read anywhere |
| 0x0c | size class: 3 = 32x24, 2 = 14x28, 1 = 28x28 | the only read is `mov dx,[eax+0x729a4]` 0x25c73 in `ents_project` 0x25a78 -> `tab6e470[class*20 + step]` |
| 0x0e | `short nframes` | `movsx eax,[ebp+0x729a6]` 0x21360 |
| 0x10 | `char name[8]` - the WACKY.DAT entry to load | `lea`/`add eax,0x10` 0x2137c feeding `datfind` 0x21384 |
| 0x18-0x23 | zero, unused | no read |
| 0x24 | `char *data` (zero in the file) | `mov [ebp+0x729bc],eax` 0x2136f, re-read 0x2138f/0x213b9 as `v7eb4c += size` |

`w * h * nframes` is the payload size, CONFIRMED by census: for all 36 records
the DAT entry named at +0x10 exists and its `size` equals `w*h*nframes` exactly
(no exception; the multiplication is 0x2134f-0x21367 and the count reaching
`fread` is read back at 0x213b6).

The id is the record index; ATR order is not file-name order, and the names run
H1,H2,H4,H5,H7,H6,H8,H9,H10,H11,H12,H14..H25 (ids 0-22) then OB1..OB13
(ids 23-35). H3.SP and H13.SP are absent from the ATR list AND from WACKY.DAT
(census of `H*.SP`: 23 such entries besides `HANDLE.SP`/`HOGMIS.SP`; `OB*.SP`:
13) - so the numbering gaps are in the archive, not in the table.

`kind` groups the 36 sets (CONFIRMED from the record data, SPECULATIVE as a
meaning): kind 0 = the 12 OB sets (one frame, never animated by `ents_show`'s
`nframes` wrap), kind 1 = OB1 alone, kind 2 = the 18 animated H sets, and kinds
3-7 one set each: 3 = id 0 (H1.SP, the only set present in all 42 `.SPW` lists),
4 = H15.SP, 5 = H12.SP, 6 = H16.SP, 7 = H20.SP. `.SPW` census over all 42 files:
838 entity records, every id 0..35 used at least once, id 0 in 69 records.
`ents_flags` 0x213f0 clears `ents[].f2` for `kind >= 2` in a one-player off-line
KID MODE race and, with `netvoices`, rewrites every `kind > 2` entity's id to
`v7eda4` = the first kind-2 record (`src/ww.c:9035-9050`).

Trap: `SPRITE.ATR` record offsets reach the disassembler as folded immediates
(`movsx eax, word ptr [eax + 0x72998]`), not as `sprattr + id*40`, so an xref of
0x72998 finds the base loads only - grep the folded offsets instead.

### .SP payloads - column-major 8-bit indexed, colour 0 transparent (CONFIRMED)

A frame of a `w x h` set is `w * h` bytes stored **column by column**: pixel
(x, y) is byte `x * h + y`, frame f starts at `f * w * h`. One byte per pixel,
no mask plane (the census above: size = `w*h*nframes` exactly). Evidence:

- The blitter `spr_draw_scaled` 0x37487 (asm module) draws in Mode X: the outer loop
  is per screen column (map mask 0x3C4 index 2 per plane, `edi++` every four
  columns, 0x374fd-0x3755a), the inner loop goes down the column (`stosb`
  then `add di,0x4f` = 80 bytes a row, 0x3751d-0x37529), skips colour 0
  (`cmp al,0; je` 0x37515 - transparency), and steps the source by a dword
  delta from the table at `p7d838` for every pixel (`add esi,[edx]; add
  edx,4`), restarting from the sprite base for every column (`push esi` /
  `pop esi`).
- Those delta tables are the `.INF` files (below): at the 1:1 step, column c
  starts at `c * h` and the deltas inside a column are +1, in all six sets.
- Rendered with that order, H1/H2/H15 (32x24) and OB2/OB5 (14x28) are clean
  pictures (a matchbox with a flame, an owl, a ball, a cattail, a barrel);
  read row-major they are diagonal smears.

Which palette the race uses is not pinned here (the render used WIN.PCX's).

### .INF - the 10-step scaling tables for sprites (CONFIRMED)

`kartsets_load` 0x28038 reads 10 steps per file into `tab6e470[set][step]`
(`kartfr` in `src/ww.c`): each step is `short w, h` (the size drawn on
screen) then `(h + 1) * w` dwords, per screen column one start offset into
the source frame followed by h per-row deltas (the last one is applied after
the last pixel and is not used). Step 0 is 1:1, steps 1-9 shrink by skipping
source rows and columns (32X24 step 9 is 9x7, deltas 3/4). The seven files
are named by their step-0 size: `251X6.INF` (ACTION.SP, `kartsets_load(1)`),
`38X28`, `28X28`, `14X28`, `32X24`, `15X13`, `18X13` (the six `kartsets[]`
names, `kartsets_load(0)`). Each file is exactly the sum of its 10 steps,
except `18X13.INF`: 4992 bytes, 4900 read; the last 92 are never read.
`SPRITE.ATR`'s size class +0x0c picks the set (`tab6e470[class * 20 + step]`,
`ents_project` 0x25c73).

### The twelve zone-less slots 16-21 and 37-42 (CONFIRMED: six duplicate pairs)

md5 over every `<n>.<EXT>` payload of the 42 sets (`tools/le/datdump.py`):

- The all-zero `.POS` is ONE 8194-byte blob shared by exactly those twelve
  slots (it is one of the 30 distinct `.POS`).
- The twelve form six pairs - (16,42) (17,41) (18,40) (19,38) (20,39) (21,37).
  In a pair the `.PAR` and the `.SIN` are byte-identical (that `.SIN` group is
  used by no other slot, except 19/38 whose `.SIN` is also that of slots 10
  and 15), and only `.M`, `.RD` and `.SPW` differ. Five pairs have an
  identical `.GAM` too; (16,42) differ in nothing but their first two order
  pairs - 16 `256,1050` / `1856,1080`, 42 `1800,196` / `210,1800`, all other
  24 lines equal (`16.GAM` 160 bytes, `42.GAM` 159).
- In all twelve the first two order pairs - the start slots `order_a[0..1]` /
  `order_b[0..1]`, each `+ 0x3e0` - sit far apart at the map edges: 21/37
  `1800,196` and `196,1800`, 19/38 `1980,126` and `126,1980`, 20/39 `1900,200`
  and `200,1860`, 18/40 `324,960` and `1776,1280` - where the 30 zone-having
  slots put them beside the six grid rows (x 190..450, y 384..1300). Order
  pairs 3..8 are a six-row grid in all 42: the y step is 96 except in 7, 15 and
  23 (64), and the x alternates between two values except in 17, 20, 39, 41
  where one row sits at x 300. `fn_16f90` 0x16f90 rewrites
  only `order_a[4..7]` when `twoplayer == 0` (`src/ww.c:4071-4081`), so pairs
  0..3 always come from the file. The same census: the twelve write line 8 as
  a bare `1` where the other 30 pad it to 17 chars - both parse to 1, no
  functional difference.
- Art families: nine distinct picture-name sets over the 42 slots and nine
  distinct `.PAR`, but not 1:1 - three `.PAR` each serve two picture families
  (`f3/f4`+`f3/f11`, `f1/f2`+`f1/f15`, `f13/f14`+`f16/f17`) and every family
  uses one or two `.PAR`. Every zone-less pair shares its `.PAR` with
  zone-having slots.
- What the engine can reach: `track` 0x7e728, the number `track_load` 0x279e8
  and `gam_load` 0x2717c turn into the `<n>.EXT` names, has exactly three
  stores in the whole image, all inside `fn_17928` - 0x17aa5, 0x17ac6 (`= 7`,
  the `v7ec80` override) and 0x17b52 (`tools/le/xref.py ww 0x7e728`). The
  switch subject is `(short)v88bb6 + v7ecb4 - 1`, guarded `cmp eax,5; ja`
  0x17a79-0x17a7c with jump table 0x17910, and its six arms copy `BRONZEM.SP`,
  `SILVERM.SP`, `GOLDM.SP`, `BONUSB.SP`, `BONUSS.SP`, `BONUSG.SP` to 0x80ace
  while assigning `v88b88 + {0, 5, 0xa, 0x15, 0x1a, 0x1f}`
  (`src/ww.c:4321-4350`). `v88b88` wraps 1..5 (`fn_1832c`, `src/ww.c:5006-5017`);
  `v7ecb4` is 0 or 3 (`zones_menu` 0x16664 stores 0 at 0x16669 and 3 at 0x167c8 -
  the only other stores, in `main_menu` 0x1f858, restore the value `save_ctl`
  0x119d8 saved: `ctl775dc` 0x775dc in split screen (0x1fdec), `ctl775da` 0x775da
  otherwise, so they hold 0 or 3 as well); `v88bb6` 1..3 is LIKELY (`fn_1832c`). An out-of-range subject takes
  the `ja` and writes nothing. The same two switch inputs are also restored from
  the saved-game records `rsave774e8` 0x774e8 (`struct rsave` fields `metal`
  +0x10, `track` +0x14, read back as words at 0x1fdf1-0x1fe4a, written by
  `race_run` 0x2fd8c, `src/ww.c:16543-16548`).
  CONFIRMED (static, and live iter 56c): the arms give 1..5, 6..10, 11..15
  (regular; 7 for the `v7ec80` case) and 22..26, 27..31, 32..36 (bonus pack:
  0x15/0x1a/0x1f = 21/26/31 plus 1..5), so **the loadable tracks are 1..15 and
  22..36 - exactly the 30 slots with a zone map - and the twelve zone-less slots
  16..21 and 37..42 are never loaded** (iter 53 said "1..30" by adding 0x15 as 15).
  Live: `v7ecb4` = 3 (what `zones_menu`'s BONUS PACK row stores; the menu itself
  only shows when `v7eca0 == 3`) and pick 1 load `track` = 0x16 = 22 with
  BONUSB.SP and the "BONUS BRONZE WHEELS" screen (state `bonus22.sav`).
- The only code that touches 31..42 does it by name: `release_check` 0x34458
  sets `v7eca0` 3 if `datfind("42.GAM")` finds an entry, 2 if `datfind("20.GAM")`
  does, else 1 (shareware) and shows "WACKY WHEELS VERSION 1.1" plus the
  release line (`src/ww.c:18516-18530`). Both sentinels are zone-less slots.
- No append fingerprint in this DAT: the `.GAM` records are directory slots
  96..137 in track order 1..42 and each slot's six payloads follow as a block
  in track order (138..389), so the file was regenerated rather than patched
  (CONFIRMED, directory census over the 577 entries).
- STRONGLY SUPPORTED: the twelve are duplicates - six art sets shipped twice.
  SPECULATIVE which release each copy was for; nothing in WW.EXE can load the
  37..42 copies.
- Functional effect in all twelve (CONFIRMED from the map bytes and `map_at`
  `src/ww.c:1511-1519`): every zone test answers class 0, `ncells` = 0 and so
  `v88ba0` = 0 (a global with no read, QUESTIONS 18). The far-apart start pair
  is the only other authored difference.

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
0x11a54) - CONFIRMED. The version string "1.1" at +2 is bytes; that it is the
version is LIKELY, `release_check` prints "WACKY WHEELS VERSION 1.1" separately.

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
= 8 and 16 records). Why each group starts at slot 0 / 4 / 7: not answerable
from the shipped programs - neither WW.EXE nor SETUP.EXE reads or writes the
records (QUESTIONS.md 16).

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
(findings/scores.md iter-50 row). CONFIRMED (SETUP.EXE, below): those zeros are
SETUP's - it never writes +0x02, +0x0a, +0x14, +0x18 or the 24 records, so a CFG
it creates carries the zeros of its uninitialised data there.

### the other two config blocks (bytes CONFIRMED, meaning OPEN)

- `cfg7b6d8` 0x18 = 12 words; in the shipped CFG they are BIOS scancodes
  `4b 4d 38 1d 50 39 20 21 1e 1f 22 23` (Left/Right/Down cursor, Alt, Ctrl,
  space, then D F A S G H). `main` stores its address into `v7d8a4` (0x35d57),
  `v7d8ac` (0x35d39), `v7d990` (0x35d4d) and `v7d8b0 = 0x7b6e4` = base+0xc
  (0x35d43); `cars_menu` 0x1a380 tests bytes of
  it through an index (`cmp byte [eax + 0x7b6d9], 0` 0x1abc2).
  CONFIRMED by SETUP (below): the key map, 12 bytes per player (player 1 at
  +0, player 2 at +0xc), six word slots each in the order steer left, steer
  right, accelerate (button 1), fire (button 2), brake (button 3), hand-brake
  turn (button 4); the low byte of a slot is the scancode. The high byte is
  LIKELY the key's live pressed state: the game reads +1 into `key88cd5` and +3
  into `key88cdd` (names file 0x7b6d9/0x7b6db), i.e. steer left / steer right.
- `cfg77128` 0x630: 56 distinct addresses inside it are touched
  (`rangexref.py ww 0x77128 0x77758`); `ctl_setup` 0x14b00 reads +0x08..+0x12
  (0x14ba6-0x14bf4), `main` tests +0x308 (`w77430 == 0xfa`, 0x36072), and the
  control names are strings inside it - the CFG bytes at 0x77432 read "LEFT
  CURSOR", "RIGHT CURSOR", "ALT", "CTRL", "DOWN CURSOR", "SPACE", "D", ...
  (findings/menus.md has the 84-byte form used by the F1 controls screen).
  It ends at 0x77128 + 0x630 = 0x77758, the address of `tbl77758`.
  CONFIRMED by SETUP (below): the names at +0x30a (0x77432) are 2 players x
  0x54 bytes = six 14-byte names, one per key slot, made from the scancodes
  (SETUP 0x19420-0x194ec); a player on a joystick gets "LEFT", "RIGHT",
  "BUTTON1", "BUTTON2" in the first four. +0x18..+0x28 are the five baud rates
  2400..115200 (`0x2580, 0x4b00, 0x9600, 0xe100, 0x1c200`), +0x54 / +0x94 /
  +0xd4 the modem init, dial (`ATDT`) and hang-up strings.

### SETUP.EXE - the program that writes WACKY.CFG (CONFIRMED, static)

`original/SETUP.EXE` is not 16-bit: like WW.EXE it is a bound DOS/4GW LE
(LE header at 0x290FC, the same stub), Watcom C/C++32 run-time, "WACKY WHEELS VER
1.1 setup. Copyright BEAVIS-SOFT 1994". Load it with `tools/le/leparse.py
original/SETUP.EXE .work/le/setup`; addresses below are its own image's (its
copies of the three blocks: 0x61708 = `cfg7ee1c`, 0x5e230 = `cfg7b6d8`,
0x48100 = `cfg77128`).

- Start (0x196cc): requires WACKY.DAT, honours `APOGEECD` like the game, opens
  `WACKY.CFG`; missing (0x198d8) or not 0xada bytes (0x1990c: "Incorrect config
  for this version", "Removing old setup CFG") -> fill the defaults (0x11d1c),
  write the file (0x19284), and read it back; it also requires `+0x308 == 0xfa`
  (0x199b9) as the game does. SETUP never reads `WACKY.ING`.
- Writer 0x19284: the same three `fwrite`s as the game's `save_config`
  (0x492 / 0x18 / 0x630); called with -1 it first rebuilds the key names.
- Defaults 0x11d1c: the 12 scancodes above, block +0x0e = 1, +0x28 = 0, +0x2a = 1,
  +0x2e = 0, +0x30 = 0, +0x32 = 1, +0x34 = 1, +0x36 = 1, +0x38 = 7, +0x3a = 0x220,
  +0x3c = 2, +0x3e = 1, +0x40 = 1; the baud and modem strings.
- Field census (`tools/le/rangexref.py setup 0x61708 0x61b9a`): **nothing in
  +0x42..+0x491** (the 24 records), and nothing at +0x02, +0x0a, +0x14, +0x18.
  Joystick calibration (0x129a4: centre, top-left, bottom-right) writes
  +0x1c..+0x26, the game's `ctl_setup` inputs; the control-options menu 0x18a98
  sets +0x2e (player one's joystick); the detail menu 0x181a8 ("High / Medium /
  Low detail") reads and writes +0x3c (`v7ee58` in the game). +0x34..+0x3a are
  set only by the defaults (0x220 and 7 look like a Sound Blaster port and IRQ:
  SPECULATIVE).

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
  presence in the archive - not its contents - is what `release_check` tests,
  and slots 31..42 are never loaded: `track` 0x7e728 has three stores, all in
  `fn_17928`. A track-number range census that greps the port for `track =`
  also hits the record-book menus, whose `track` is a local.
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
- The `.GAM` picture names are not DAT entry names: the exe prepends `A_`
  (`src/ww.c:12143-12151`) and `datfind` uppercases. Asking the archive for
  `f1.pcx` finds nothing; `A_F1.PCX` is the entry.
- Two different "maps" per track. The surface class under a point comes from
  `.M` (tile indices) + `.SIN` + the mask half at `v7d75c + 0x1b000`
  (`surface_at` 0x37b62-0x37baf); the `.POS` zone classes and their `ncells` are a
  separate lookup (`map_at`, `src/ww.c:1513`). Indexing `.SIN` by a `.POS` cell
  value is wrong - it is indexed by tile index, and 108 = the number of tiles
  in one half of the tile area.
- `.SIN` is stride 12 in the file and stride 8 in memory (`sin7ad88[0x6c][8]`),
  and the file's leading dword of every record is read into a scratch and never
  used again; a port that parses the record as three dwords from +0 indexes the
  wrong pair (the live pair is file +8 and +12).

## The face record and the tables fn_173c4 fans it into (CONFIRMED, iter 59)

`fn_173c4` (0x173c4) loads one player's record (`datfind(arr7bba0 + myplayer*0xc)`, `fread` into
`v7d6c8`) and fans fields of `faces[myplayer]` (0x73c08, 0x98-byte records) into per-player pointer
tables: eight dwords from `faces+eax+0x3c` into the table at 0x7b324
(`mov edx,[ebx+eax+0x73c44]` 0x17594) and eight from `faces+eax+0x58` into 0x7b2d0
(`mov ebx,[ebx+eax+0x73c60]` 0x175cb), and the eight-write fan 0x17661-0x17714 writes the
animation table at **0x7b238** (dwords 0x7b238..0x7b270, indexed `v88b7e*4` + 0/8/.../0x38,
sources img[8],9,8,4,10,11,10,4). 0x7b238 is the base stored into `curkart+0x4e` at 0x3174a and
walked by 0x2bff0 (`tools/le/xref.py 0x7b238`); no instruction in the image mentions 0x7b230.
Our port had all three addresses wrong (table written at 0x7b230, fans reading +0x44/+0x60) until
iter 59 - the byte diff never moved (src/ww.c fn_173c4 note, `BEHAV:` row in src/nearmiss.txt).

## The PCX writer `screenshot` 0x114c8 (CONFIRMED, iter 80)

`difffn.py screenshot CALLS=20000 COUNT=12000000` runs the whole function in both images: it
**returns** (807,314 insns at 184 addrs under `hotfn.py`, 65,200 passes of the RLE inner loop =
200 rows x ~326) and the two sides end with every compared byte equal (control
`NEW=wworig MAP=.work/link/WWORIG.MAP` SAME). No global seed is needed - the old "CALLCAP 300/300"
was only the `fputc` count: 200 rows of the screen emit ~2 `fputc` each plus the header writes, so
300 stubs is not enough to reach the `ret` 0x11805.

- CONFIRMED (0x11700-0x117c3): the body is 200 rows - `inc edi; cmp di,0xc7; jle 0x11700` - and each
  row pulls itself into the local buffer at `[esp+0x320]` through `call spr_grab` 0x11730, after
  0x11700-0x1172b set `spr_x` 0x88b32 = 0, `spr_h` 0x88b36 = 0x140, `spr_w` 0x88b26 = 1,
  `spr_y` 0x88b34 = the row counter and `sprptr` 0x7d704 = `[esp+0x508]`. So the writer captures a
  320 x 200 window, not the whole 0x140x0x1e0 screen.
- CONFIRMED (0x11735-0x117b7): PCX run-length encoding with the run capped twice -
  0x1174f `cmp eax,0x140` (a run may not pass the end of the row) and 0x11756 `cmp bx,0x3f`
  (max run 63); a run of n > 0 is written as `n | 0xc0` then the byte (0x11764 `or al,0xc0`,
  `fputc` 0x1176b, `fputc` 0x1177d, `add ecx,ebx` 0x11782), a single literal byte is escaped when its
  top two bits are set - 0x11790 `and al,0xc0; cmp al,0xc0` then `fputc(ebp)` 0x1179a with
  **`ebp` = 0xc1** (loaded at 0x116df, not 0xc0) - before the byte itself (0x117a4-0x117ad).
- CONFIRMED (0x117d0-0x117f4): the tail is `fputc(0xc)` (the standard PCX palette selector),
  `fwrite` of 0x300 bytes from `[esp+0x50c]`, `fclose`, then `call nosound` 0x117f4 - taking a
  screenshot silences the sound - and the epilogue `add esp,0x514` + 6 pops + `ret` 0x11805.

## `gam_load` 0x2717c's line reader and the fade step (CONFIRMED, iter 80)

- CONFIRMED (`tools/wc/difffn.py fn_17928 FREAD=13 SEED=0x88cd0=1:b,0x7ebc0=1:d,0x88c46=4:w` prints
  SAME with both sides returning; control `NEW=wworig MAP=.work/link/WWORIG.MAP` with the same env
  SAME; `hotfn.py` with those seeds: 303784 insns at 1579 addrs, `returned`): `read_line` 0x26ff0 is a
  **CR-terminated** reader - it calls `fread(ptr, 1, 1, fp)` once per byte and stops when the byte it
  just read is 0x0d (0x27008 `cmp byte ptr [esi],0xd`), then does one more 1-byte read to eat the LF
  and NUL-terminates (0x27020 `mov byte ptr [esi],0`). `gam_load` 0x2717c reads five lines into the
  globals 0x7eabc, 0x7eaca, 0x7eaa0, 0x7eaae and 0x6a4e0 (0x271fa-0x27245), then the record count into
  a **local** buffer at its own frame base (0x2724a `mov eax, esp`, parsed at 0x2725f
  `movsx edx, word [esp+0x60]`, `imul edx,edx,0xe` 0x27264 - so a .GAM record is 14 bytes).
- CONFIRMED (`difffn.Run` stub, `tools/wc/difffn.py`, new `FREAD=<byte>`): with the fread stub
  returning 0 and writing nothing, `read_line`'s loop never sees 0x0d and ESI walks up through the
  callee's saved registers until it faults (`write_prot at 0xf00000`), which is what kept
  `gam_load` - and with it `fn_17928` - from ever returning. `FREAD=13` makes the stub write 13 at its
  destination **only for one-element reads** (`edx == 1`, the Watcom size register), so the bulk reads
  of palettes and PCX bodies (size 0x80 / 0x300) stay untouched and every .GAM line reads as empty.
  The two `fread` addresses are taken from the maps, not typed: `WWORIG.MAP` `0001:00032c68 fread_` +
  code base 0x10000 = 0x42c68 (the `call 0x42c68` in the disassembly) and `WWNEW.MAP`
  `0001:0000c22a fread_` + 0x10000 = 0x1c22a, whose first six bytes equal the game's.
- CONFIRMED (0x36334 `mov word ptr [0x88c46], 4` in `main`): `v88c46` is the palette-fade step, and an
  emulator that starts a function directly never runs `main`, so it reads 0. `fade_out` 0x1bf94 then
  never converges: its per-entry test 0x1c03d `cmp byte ptr [esp+eax],0` / 0x1c04d `xor edi,edi` marks
  the pass "changed" for every entry above 0, `sub byte ptr [esp+eax], cl` 0x1c05d subtracts the step
  (0), and `call dac_load; test di,di; je 0x1c017` 0x32cdd-0x32ce5 repeats the 768-entry pass forever
  (1,190,831 passes of 0x1c03a before the instruction cap). Seed `0x88c46=4:w` and the fade finishes
  in 14 passes. `fade_in` 0x32cf8 reads the same global (0x32d78, 0x32d95).

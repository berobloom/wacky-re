# Video: mode, palette, the page offsets `pal_a`-`pal_d`

INDEX question: "Video: mode, palette, blitter, back buffer, present". Status: **OPEN**
(every routine of the assembler module is read and named, below; the C side's frame and
page logic is not all pinned).

## `page_copy` (0x36f14-0x36f54) is a screen copy, not a palette load — CONFIRMED

The asm, read in full:

```
pushal
mov dx,0x3ce ; mov ax,0x4105 ; out dx,ax      ; Graphis: Chain4 write + read map 0
mov dx,0x3c4 ; mov ax,0x0f02 ; out dx,ax      ; Sequencer: write map mask = all four planes
mov edi,0xa0000 ; add di, word [0x88b1a]      ; pal_c: destination page offset
mov esi,0xa0000 ; add si, word [0x88b1c]      ; pal_b: source page offset
mov ecx,0x3e80 ; rep movsb                    ; 16000 bytes = one 320x200 page
mov dx,0x3ce ; mov ax,0x4005 ; out dx,ax      ; Graphis: undo
popal
ret
```

So `pal_a`/`pal_b`/`pal_c`/`pal_d` (0x88b24/0x88b1c/0x88b1a/0x88b18) and
`save_a`/`save_b`/`save_c` (0x88b04/0x88b12/0x88b06) are **page offsets into A000:0000**, not
palette values: the name in the old notes is wrong. `48000 = 3 * 0x3e80` is the fourth page.
`pal_a` is not used by this routine but the menus set it with `pal_b`/`pal_c` (0x1c09b-0x1c0a1),
and `fn_17888` sets it to 0 and 0x3e80 (names line 546) — LIKELY the offset the *display* reads
(read by the asm at 0x36f55 or by the retrace), SPECULATIVE.

Two helpers wrap the copy and are called at 11 sites each (menus, `cars_menu`, `modem`):

- `fn_1c068` (0x1c068): saves `pal_a/b/c` into `save_a/b/c`, sets `pal_c = 48000` and
  `pal_a = pal_b = pal_d`, then a tail `jmp page_copy` (0x1c0a7). CONFIRMED (bytes).
- `fn_1c0ac` (0x1c0ac): the reverse — `pal_b = 48000`, `pal_a = pal_c = pal_d`, `call
  page_copy` (0x1c0c7), then `pal_b/c/a = save_b/c/a`. CONFIRMED (bytes).

Both were rebuilt byte-identical, then taken out of `src/ww.c` again: defining them there lets
`-oe` inline them into every caller and `net_end` (0x110d4, byte-identical) stops matching —
see GOTCHAS. Keep them as `extern` declarations until the game's inline threshold is known
(QUESTIONS.md 6).

## The page flip every menu loop ends with — CONFIRMED

Six sites read in the rebuilt functions show one idiom, always as two stores:

```
cmp word [pal_a], 0 / jne +13 / mov word [pal_a], 0x3e80 / jmp +11 / mov word [pal_a], 0
```

at 0x1a744, 0x1a9ce, 0x1aeb9, 0x1bb0c (`cars_menu`) and 0x1cbf8, 0x1cd6a (`enter_name`,
findings/scores.md): `pal_a` is one 320x200 page offset, either 0 or 0x3e80, and the flip
selects the other one. 18 sites in the code object have that `cmp word [X],0 / jne / mov word
[X],0x3e80` shape (scan of `.work/le/ww.obj1.bin` with regex `\x66\x83\x3d....\x00\x75..` before
`\x66\xc7\x05....\x80\x3e`). CONFIRMED that the game wrote it as an `if`/`else` *statement* and
not as a ternary assignment: the same scan finds 55 `mov word [mem],0x3e80` sites but ZERO
`mov eax,0x3e80` (`\xb8\x80\x3e\x00\x00`), and Watcom 9.5b turns
`x = c ? 0 : 0x3e80` into exactly that register form (GOTCHAS).

## `pal_shift` 0x114a8 scales 768 palette bytes x4 in place — CONFIRMED

Rebuild byte-identical: `src/check.sh` MATCH, 32 bytes, 0x114a8-0x114c5.

- The loop is `i = 1; i <= 768; i++` (0x114aa `mov ebx,1`, 0x114bc `cmp bx,0x300`, 0x114c1
  `jle 0x114b0`), i.e. 768 = 256 RGB triples; every byte is shifted left by 2 in place and
  truncated to a byte (0x114b0 `movzx dx, byte [eax]`, 0x114b5 `shl edx,2`, 0x114b9 `mov byte
  [eax-1], dl`). Whether the input is 0-63 (6-bit) or 0-255 is NOT pinned: the x4 overflow of a
  byte is not what a 6-bit-to-8-bit scale would do, so the caller matters (QUESTIONS 15).
- The C that compiles to it is `v = *p; v <<= 2; *p++ = v;`. The obvious single expression
  `*p++ = v << 2` shifts only the byte (`shl bl,2`) and swaps the two registers (`i` in EDX, `v`
  in EBX) — 6 diff rows. The register choice followed the expression shape, it was not the
  coin-flip tie-break (GOTCHAS iter 19).
- Nothing reaches it: no `call rel32` with target 0x114a8 anywhere in the two code objects and no
  dword 0x114a8 in either (scan of `.work/le/ww.obj1.bin` and `.work/le/ww.obj2.bin`), so it is
  dead code, or reached through a computed address — QUESTIONS 15.

## `pcx_blit` 0x375cd takes no arguments and preserves every register — CONFIRMED

- Bytes: `pushal` at 0x375cd, `popal; ret` at 0x376d5. It reads its source from `p7d714` (0x375f5
  `mov esi,[0x7d714]`) and the page offset from `pal_a` (0x375ee `add di,[0x88b24]`), writes planar
  VGA at 0xa0000 through sequencer port 0x3c4 (0x375d9-0x375e1), 200 rows (`cmp word [0x88b58],0xc8`
  0x37659) with `v88b4a` added per row (0x3764b). Data bytes with the top two bits set are runs
  (0x3760a, count in the low six bits, 0x3766b): a PCX-style RLE decode straight to the screen.
  LIKELY: it is the PCX picture blit; `p6a978`/`p6a980` hold the encoded pictures.
- So the C prototype is `void pcx_blit(void)`. The callers' `mov eax,[p6a9xx]` before the call is the
  `p7d714 = p6a9xx` store, and `mov dx,[pal_a]` ... `mov [pal_a],dx` is a `short` save of pal_a that
  DX keeps across the call (0x1ce57/0x1ce6c, 0x1d73e/0x1d758, 0x1e816/0x1e829). CONFIRMED by
  rebuild: switching `src/ww.c` to this prototype removed those rows in credits_screen, demo_mode,
  main_menu and 6 other functions (iter 36).

## `horizon_upload` 0x36f55 - the second map-mask writer: 0x6400 bytes to A000:7d00 — CONFIRMED (disasm)

- `pushal`/`popal`, no arguments, source `[0x7eb54]`: `mov edi,0xa0000` + `add di,0x7d00`
  (0x36f5e-0x36f63), `mov cx,0x6400` (0x36f68), then per source byte a write of `[edi]` plus
  `shl ah,1; cmp ah,8; jle loop` and, only when the rotated value passes 8, `inc di` and `ah=1`
  (0x36f79-0x36f86), with the sequencer map mask (port 0x3c4 index 2) re-programmed to `ah` at
  each iteration. So the mask runs 1, 2, 4, 8 and 25600 writes cover 6400 video
  addresses (A000:7d00..0xe0ff), each written once per plane: the source holds 4
  bytes per video address (CONFIRMED by the loop shape). `page_copy` programs
  `0x3ce ax=0x4105` (chain-4 write, line 12 above) and the game sets INT 10h
  `0x13` (`setvmode_keep(0x13)`, `src/ww.c:19453`), and the four map-mask writes
  go to planes 0..3 of one Mode X address (CONFIRMED live, findings/formats.md ".PAR").
- 0x7d00 = 32000 = the start of the third 0x3e80-byte page. The blob is a 1280x20 horizon
  panorama stored off-screen there; every race frame the asm copier 0x395dd latch-copies a
  20-row, 80-address (320-pixel) window of it from `v7d860` to screen rows 110..129 (`pal_a +
  0x2260`) - CONFIRMED live (findings/formats.md ".PAR").
- Callers pass `p6a980` and guard on `v7d998` being 0 or 3 (`src/ww.c:4416-4417` where `p6a980`
  holds the `<n>.PAR` blob, `src/ww.c:17259-17260`, `src/ww.c:17479-17480`): the image is
  blitted once at race start, not per frame.

## `clipy` 0x7d878 is set from a value kept across the `spr_w` store — CONFIRMED (disasm)

Every bottom-clip helper computes the overflow once and uses that one value for the shortened width
AND for `clipy`; nothing recomputes it after the width store:

- `players_ahead` 0x34208: `mov ax,[spr_w]; dec eax; add ax,[spr_y]; sub eax,0xc7` 0x34332-0x34340,
  `sub word [spr_w],ax` 0x34346, `cmp word [spr_w],0 / jle` 0x3434d, then `cwde; lea eax,[eax*4];
  mov [0x7d878],eax` 0x34357-0x3435f - the same EAX, so `clipy = (spr_w + spr_y - 1 - 199) * 4` with
  the pre-clip `spr_w`. The scale factor feeds the asm blit `spr_draw_scaled` 0x37487 (names file: a scaled
  Mode X sprite blit stepped by the `p7d838` delta table); why *4 there is not pinned.
- `drawsprite_clip` 0x11404: same expression at 0x11434-0x1143e, but `mov [0x7d878],ebx` 0x11447
  stores it **unmultiplied** for the asm drawer `spr_draw_clip` 0x3707e, and `spr_w` is restored from
  a local after the call (0x11471).

Port bug fixed iter 57: our `players_ahead` wrote the expression twice, so its second read of
`spr_w` saw the shortened width and `clipy` was always 0 - no row of an over-tall sprite was ever
skipped. Row count moved 30 -> 27 by itself, but only the racetest-style behaviour check would have
caught the wrong value; a clipped sprite is one a player sees.

## The race HUD box loses its bright-yellow figure in the rebuild — LIKELY (screen diff, iter 71)

`tools/link/racetest.sh` now screenshots every menu Enter and, 3 s after the overview's dismiss key
(`spc`), the pre-race grid; that screen is compared with `captures/race_play_ww.png` (made from
`.work/link/WWORIG.EXE`). Our grid reads 2.22% bad pixels against the original's own run-to-run
floor of 1.2-2.1% (two WWORIG runs; the karts on the start line bob, rows 320-367).

The excess is one block: shot px x 50-95, y 26-82 (game px 25-47, 13-41), inside the HUD box drawn
by `draw_status` 0x12f80 as `img7d83c` (41x38 at `spr_x` 0x11, `spr_y` 6; stores 0x1313a-0x13168,
`call spr_draw` 0x13168). Colour census of the box region (shot px x14-116, y8-96):

| | pure yellow (255,255,0) | (239,195,0) | black |
|---|---|---|---|
| the game | 1380 px | 536 px | 1072 px |
| our build | 0 px | 540 px | 2272 px |

so the box frame matches and the figure the game paints inside it - about 1380 shot px, i.e. some
345 game px of full-bright yellow, one big digit at most - is never drawn by us; we paint black
there instead. Census: `/tmp`-free one-liner, slice `[8:96,14:116]` of the two shots and count the
colour triples - `from PIL import Image; import numpy; a=numpy.asarray(Image.open(P).convert('RGB'))[8:96,14:116]; import collections; print(collections.Counter(map(tuple,a.reshape(-1,3))).most_common(6))`. LIKELY a draw the game makes and we dropped, not a palette or fade difference:

- Stable in both builds at 3 s and at 9 s after the start key (identical pixel counts), so it is a
  state, not an animation or fade phase.
- The same screen region on the track-overview screen is pixel-identical (the overview pair reads
  0.00%), so the sprite, its coordinates and the palette are right in the C; what is missing belongs
  to the race-time state.
- `draw_status`'s own reference census shows no one-sided address (nmrefs 107/108 `counts`, all
  adjudicated in iter 70) and `race_run` 0x2fd8c / `overlay_draw` 0x2a92c read `same`, so the
  missing draw either lives in another function or reaches its sprite through an index-folded table
  address, which cancels in a refs census.

QUESTIONS 25. Owners to try first: `race_run` 0x2fd8c, `overlay_draw` 0x2a92c, `lights_project`
0x266a8.

## The HUD box's figure is the `place` icon; the C reads the right slot, the state is wrong — iter 72

Draw site (CONFIRMED, `overlay_draw`): 0x2aec8-0x2af50 — when the `one` local (`[esp+0x2c]`, test
0x2aec2) is set, `spr_h = 0x16`, `spr_w = 0x1d`, `spr_x = 0x19`, `spr_y = 0x6f` if `v88bde` else
`0xc` (0x2aeed `mov [spr_y],0x6f`, 0x2af8 `mov [spr_y],0xc`); per viewer
word `viewer7eb80[i]` (0x2af07 `cmp word [ecx+0x7eb80],0`, ecx steps 2, `ebx` steps 0x104) it reads
the index 0x2af11 `movsx eax, word [ebx+0x684bc]` = `cars[i].place` (= `karts[i].fac`, 0x684bc /
0x685c0) and 0x2af18 `mov eax, dword [eax*4 + 0x6e6ec]` -> `sprptr`; single viewer draws through
`spr_draw_opaque` 0x373ee (no colour-0 test), splitscreen through `spr_draw` 0x36fd0 with `spr_x = 4`
(0x2af40). So the box figure is 22x29 at game px (25,12), i.e. exactly the bad block of
QUESTIONS 25 (shot px x50-93, y24-87).

Geometry (CONFIRMED, the drawer is column-major): both drawers load `spr_w` into `bh` as the inner
loop (0x37012/0x37435 `mov bx,[spr_w]`, `add di,0x4f` per byte -> one video byte per *screen row*)
and `spr_h` into `cx` as the outer loop (0x37021 and 0x3743e `mov cx,[spr_h]`), rotating the
GC map-mask plane once per outer iteration and `inc edi` when it wraps. The drawn rectangle is
therefore `spr_h` columns wide by `spr_w` rows tall, and the cell is column-major:
`byte = col * spr_w + row`. Check: the box (`spr_h` 0x26, `spr_w` 0x29 at 17,6) and the icon
(`spr_h` 0x16, `spr_w` 0x1d at 25,12) both land where the screenshots show them.

The table (CONFIRMED): `0x6e6ec[1..8] = lapdata + 0x27e*k` (or `lapdata + 0x166e + 0x27e*k` when
`v88bde`), written only by `fn_17928`'s tail, 0x17a35-0x17a69 (`xor edx,edx` then `add edx,0x10`,
four stores `[edx+0x6e6e0..0x6e6ec]`, `cmp edx,0x20; jne`), guarded by 0x17a07 `twoplayer == 0` and
0x17a11 `netvoices == 0`. Slot 0 (0x6e6ec itself) is *never written*, so `place == 0` means
`sprptr == NULL`. Rebuilt in C at src/ww.c:4412-4421 with the same guard, the same 0x166e and the
same slots 1..8.

The data (CONFIRMED by file, not by trace): the `LAP.SP` record of WACKY.DAT (size 0x4c56, offset
6944451) is eight-plus cells of 0x27e = 638 = 22*29 bytes; cell 8 (lapdata + 7*0x27e = 0x1172) is
278 px of palette index 83 and 360 px of index 46, and rendering it column-major draws the
seven-segment "8" pixel-for-pixel as `captures/race_play_ww.png` shows it (278 game px = 1112 shot
px of (255,255,0), the count measured in the iter-71 census). So the original draws `place == 8`
there, and the icon *is* the race place.

Our build (CONFIRMED that it was wrong; cause found iter 73, below): the same 22x29 rect is painted, but with
garbage — 45-69 % of its bytes read as index 0 (black) and the rest as skin/blue/cyan speckle
(`/tmp` census of `.work/link/racetest_race.png[24:82,50:92]`), while *no* `LAP.SP` cell contains a
single index-0 byte (each cell is 360 x 46 + 278 x 83 / 45). So `sprptr` is not
`lapdata + 0x27e*k`: either the slot read is 0 (NULL) or a slot of the table was overwritten.
The code on both sides is equal, so this is a state difference, not a shape difference:
`fndiff overlay_draw` has no line touching 0x6e6ec; `tools/link/symmap.txt` gives
`_arr6e6ec 0x06e6ec`; the fill site matches (above); `cars[].place` is written only by
`calc_places` 0x128a4 (MATCH; the single-player arm clamps 1..8 at 0x129e6-0x12a08) and by
`race_run` 0x3021e-0x3023b (8 if `nplayers > 1` else 1), and no writer of `place` is a near miss.
Nearest suspect, unproven: the tables below are one contiguous block — framesets at 0x6e470,
the 0x6e5b0 countdown table, then the place icons at 0x6e6ec = 0x6e470 + 4*159 — and the only
variable-index writer into it is `kartsets_load` 0x280e2 `mov [ebp+eax*4+0x6e470], edx` (inner
index `di` with 0x28155 `cmp di,9`, outer 0x2816c `cmp eax,[esp+8]; jl 0x280b0`), a 20-row near
miss: an index or bound that runs 159+ there would overwrite the place slots with frame pointers,
which is what a colourful noise block looks like.

Handoff experiment (no new tooling): `ORIG=<fn> tools/link/racetest.sh` builds the hybrid with
those C functions taken from the game's bytes, so it bisects state bugs by function — run it with
`ORIG=kartsets_load`, then `ORIG=fn_17928`, then `ORIG=overlay_draw`, and after each run render
`.work/link/racetest_race.png[24:82,50:92]` with the census one-liner above: the fix shows up as
278 px of (255,255,0) (place 8) appearing again.

**Cause (iter 73, CONFIRMED by bisect and fix):** the bisect put it in `fn_17928` (ORIG=kartsets_load
and ORIG=overlay_draw: 4 yellow / 1288 black shot px; ORIG=fn_17928: 1112 / 88, the original's own
count). The filler's four stores were the game's bytes without the game's fixups (0x17a3c, 0x17a47,
0x17a52, 0x17a5d): DOS/4GW loads DGROUP away from its link base, so our stores wrote the raw link
address and the relocated table `overlay_draw` reads (fixup 0x2af1b) stayed NULL; slot NULL draws
whatever sits at linear 0, hence the black-and-speckle block. The frameset hypothesis above was wrong:
`kartsets_load`'s highest index is 5*20+9 = 109. Fixed through linker symbols (src/ww.c, `placeic_c`);
`tools/link/fixcensus.py` finds every such literal by comparing per-target fixup counts.

## The assembler module's routines, read and named — iter 91

Every routine of 0x36A4E-0x409EE (`src/wwasm.asm`) was read in the asm. One line each, with level and
evidence, is in `analysis/ww_names.txt` (the block at the end) and above each routine in the asm.
The C side was renamed in the same commit: only names changed, so `src/check.sh` still gives 207 and
the link still resolves (1174 externals).

| Address | Name | Was | Level | What |
|---|---|---|---|---|
| 0x36a4e | `textscreen_show` | f36a4e | CONFIRMED | 4000-byte text screen from [p7d714] to B800 (exit screen) |
| 0x36cca | `getpalette` | | CONFIRMED | 256 DAC entries to [palptr] |
| 0x36d42 | `rowtab_init` | f36d42 | CONFIRMED | [v7eb84][y] = y*80, 320 rows |
| 0x36e36 / 0x36e53 | `vga_latch_on` / `vga_latch_off` | L_ | CONFIRMED | write mode 1 + all planes / write mode 0 |
| 0x36e66 | `rows_restore` | f36e66 | LIKELY | latch copy of spr_w rows from VRAM 0xcc60 to row spr_y |
| 0x36ec2 | `band_mirror` | f36ec2 | CONFIRMED | 90-row band at spr_y mirrored vertically |
| 0x36f14 | `page_copy` | setpalette | CONFIRMED | page pal_b -> page pal_c (above) |
| 0x36f55 | `horizon_upload` | fn_36f55 | STRONGLY SUPPORTED | linear [v7eb54] -> planar 0x7d00 (below) |
| 0x36f8c | `rec_swap` | f36f8c | CONFIRMED | swap two records (the sorts) |
| 0x36fd0 / 0x3707e / 0x37132 / 0x373ee | `spr_draw` / `_clip` / `_key` / `_opaque` | drawsprite, drawsprite2, f37132, f373ee | CONFIRMED | column-major sprite: colour 0 transparent / skip clipy per column / v88ce9 transparent / opaque |
| 0x371e4 | `box_draw` | f371e4 | CONFIRMED | border v88ce8, fill v88ce9 |
| 0x372c7 / 0x3735c | `spr_grab` / `grab32` | getline+blit372c7 / f3735c | CONFIRMED | VRAM -> [sprptr], column-major / 32x32 row-major |
| 0x37487 | `spr_draw_scaled` | f37487 | STRONGLY SUPPORTED | scaled sprite (formats.md .SP) |
| 0x375cd | `pcx_blit` | f375cd | LIKELY | PCX RLE to the page (above) |
| 0x376d7 | `pcx_tiles` | f376d7 | CONFIRMED | PCX 320x200 -> 54 tiles of 32x32 at [v7eb58] |
| 0x377ae / 0x3784b | `buttons_poll` / `joy_poll` | f377ae / joy_detect | CONFIRMED | buttons; game-port axes -> direction keys (`joyfound` 1 = no stick) |
| 0x37a06 | `tilemap_fill` | f37a06 | CONFIRMED | [v7d6a4] = v7d64c, 64 KB |
| 0x37a21 / 0x37bb1 | `path_pt_scale` / `vec_scale` | f37a21 / f37bb1 | LIKELY / CONFIRMED | 16.16 rounded scaling |
| 0x37b04 | `surface_at` | f37b04 | CONFIRMED | surface probe (formats.md, game-loop.md) |
| 0x37bf0 | `pixel_plot` | f37bf0 | CONFIRMED | one pixel, colour v88ce9 |
| 0x37c43 / 0x37c84 / 0x37cf1 | `dac_put5` / `dac_put8` / `dac_load` | f37c43 / f37c84 / f37cf1 | CONFIRMED | palette animation entries / a range from [palptr] |
| 0x37de0 | `page_show` | waitretrace+f37de0 | CONFIRMED | the page flip: CRTC start = pal_a, pal_d = pal_a |
| 0x37e23 / 0x37e41 | `vram_clear` / `modex_init` | f37e23 / f37e41 | CONFIRMED | clear 256 KB / unchain to Mode X |
| 0x37e94 / 0x3c3e9 | `floor_draw` / `floor_draw_upper` | f37e94 / f3c3e9 | STRONGLY SUPPORTED | horizon + perspective floor from row 110 / row 10 |
| 0x40907 | `kbd_isr` | | LIKELY | keyboard IRQ handler |

- **The perspective floor is three unrolled copies per entry, ~2,070 instructions each.** `floor_draw`
  and `floor_draw_upper` pick one by `v88c20`, the detail setting (1/2/3 = the High/Medium/Low detail
  menu, formats.md +0x3c). Six bodies, 33 KB of the module's 40 KB. Per pixel: a precomputed point
  `[ebx]` times the scale `[esi]`, plus the camera `v88b0a`/`v88b0e`, gives a tile of the map
  `v7d6a4` (32x32 tiles, 128 per map row) and one byte from it. CONFIRMED that shape (instruction
  census).
- **The three bodies differ only in horizontal resolution** (CONFIRMED, iter 91: the bodies diffed
  with labels normalized, 2071-2097 lines each). Each body is one screen column of **70 floor rows,
  unrolled** bottom to top (`movsb; sub edi,0x51` = one row up), in a loop over columns:

  | `v88c20` | label | columns | pixels per column | plane mask per column | points used (`ebx +=`) | scale entries skipped (`esi +=`) |
  |---|---|---|---|---|---|---|
  | 1 High | `floor_hi` 0x37ece | 0x140 = 320 | 1 | 1,2,4,8 rotating (`shl ah,1`) | 8 (every point) | 0 |
  | 2 Medium | `floor_med` 0x395dd | 0xa0 = 160 | 2 | 3, 0xc (`shl ah,2`) | 0x10 (every 2nd) | 0x118 = 70*4 |
  | 3 Low | `floor_lo` 0x3ad0f | 0x50 = 80 | 4 | none: 0x0f from the entry | 0x20 (every 4th) | 0x348 = 3*0x118 |

  So the point table at `[ebx]` has 320 entries of 8 bytes (one per screen column) and the scale
  table at `[esi]` 70 dwords per column (one per floor row). After a column `edi` goes back down
  0x15e0 = 70 rows (+1 address on Low, and on High/Medium when the plane mask wraps).
- **Screen rows:** `floor_draw` copies the horizon (20 rows of `[v7d860]`, 80 of each 320 addresses)
  to rows 110-129 and draws the floor from row 199 (`edi` 0x3e30) up to row 130; `floor_draw_upper`
  (`floor_upper_hi/med/lo`) is the same code with rows 10-29 and 99 (0x1ef0) up to 30, the
  two-player top view. CONFIRMED (the only differing lines are those offsets and the arms below).
- **`v7d998` arms only in `floor_draw`, and not the same per detail** (CONFIRMED the bytes): High
  skips the horizon copy when `v7d998` == 1; Medium, when `v7d998` == 2, instead copies 0x28a0 bytes
  (130 rows) from VRAM 0x7d00 to the top of the page and skips the horizon; Low has neither.
  `v7d998` 1/2 are the record/playback modes (formats.md), in which the horizon strip is not loaded.
  Why the arms differ per detail level is unknown (QUESTIONS 29).
- **Six routines nothing reaches** (no call, jump or LE fixup; CONFIRMED by the census in
  findings/executables.md "Open"): `strips_restore` 0x36a69, `rect_overlap` 0x36d69, `pt_load` 0x379ec,
  `surface_at_7c` 0x37a6c, `tile_show` 0x37d96, `vretrace_wait` 0x37dc7. They are labelled in the asm
  and kept, because the bytes are the game's. A port can drop them.
- `pal_a`-`pal_d` keep their old names (page offsets, above). Renaming the data is a separate step.

## Traps

- The drawer is column-major: `spr_draw`/`spr_draw_opaque` put `spr_w` bytes in a *column* and iterate
  `spr_h` columns (`add di,0x4f` per byte), so a sprite declared `spr_w = 0x1d, spr_h = 0x16` is 22
  px wide and 29 px tall, and its cell is `byte = col*spr_w + row`. Reading `spr_w` as the width
  makes the LAP place icons look impossible (iter 72: three readings of the box geometry were wrong
  before the drawer was read).
- A `MATCH` reader plus a `MATCH` filler still draws the wrong thing when the table between them is
  written by a *third* function: check the drawn bytes against the data file, not the code.
- A QMP screenshot is 2x the game's screen: the shots are 640x400 and the game runs 320x200, so a
  `spr_draw` at `spr_x` 0x11, `spr_y` 6 with `spr_w` 0x29, `spr_h` 0x26 lands at shot px
  34-116 x 12-88. Compare a sprite and a screenshot block with that factor or the box you crop
  holds a different widget (iter 71: three crops read before the 2x factor was applied).
- A clip line written twice in C is not the game's: the game keeps the value in a register across
  `sub [spr_w],ax` (0x34346) and reuses it for `clipy` (0x3435f). Re-reading `spr_w` after it was
  shortened gives 0. Use the local (`n = spr_w - 1 + spr_y - 0xc7; spr_w -= n; ... clipy = n * 4;`,
  the house spelling at `src/ww.c:11353`).
- A ternary is not the game's flip: `pal_a = pal_a ? 0 : 0x3e80;` selects the value in EAX and
  stores once, which no site in the game does. Write the two-branch `if`/`else` (see above);
  that is worth 71 aligned instructions in `cars_menu` alone.
- `page_copy` clobbers nothing the C sees (`pushal`/`popal`) but its `rep movsb` runs with all
  four write planes enabled: it is a whole-page copy, so calling it is never cheap (0x3e80
  bytes) — it does not belong inside a per-frame loop.
- The two helpers are *tail-call* / *call* variants: 0x1c068 ends `jmp page_copy` (no frame,
  nothing to restore), 0x1c0ac uses `call` because three stores follow. Watcom only emits the
  `jmp` when the callee follows immediately in the caller's tail — a port that writes
  `page_copy(); return;` in a frameless void function must produce `e9 ...`, and a
  near-miss `e8 ... c3` there is that difference, not a bug.

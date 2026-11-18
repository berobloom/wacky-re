# Video: mode, palette, the page offsets `pal_a`-`pal_d`

INDEX question: "Video: mode, palette, blitter, back buffer, present". Status: **OPEN**
(only the `setpalette` asm and what it moves are pinned).

## `setpalette` (0x36f14-0x36f54) is a screen copy, not a palette load — CONFIRMED

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
  `pal_a = pal_b = pal_d`, then a tail `jmp setpalette` (0x1c0a7). CONFIRMED (bytes).
- `fn_1c0ac` (0x1c0ac): the reverse — `pal_b = 48000`, `pal_a = pal_c = pal_d`, `call
  setpalette` (0x1c0c7), then `pal_b/c/a = save_b/c/a`. CONFIRMED (bytes).

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

## `f375cd` 0x375cd takes no arguments and preserves every register — CONFIRMED

- Bytes: `pushal` at 0x375cd, `popal; ret` at 0x376d5. It reads its source from `p7d714` (0x375f5
  `mov esi,[0x7d714]`) and the page offset from `pal_a` (0x375ee `add di,[0x88b24]`), writes planar
  VGA at 0xa0000 through sequencer port 0x3c4 (0x375d9-0x375e1), 200 rows (`cmp word [0x88b58],0xc8`
  0x37659) with `v88b4a` added per row (0x3764b). Data bytes with the top two bits set are runs
  (0x3760a, count in the low six bits, 0x3766b): a PCX-style RLE decode straight to the screen.
  LIKELY: it is the PCX picture blit; `p6a978`/`p6a980` hold the encoded pictures.
- So the C prototype is `void f375cd(void)`. The callers' `mov eax,[p6a9xx]` before the call is the
  `p7d714 = p6a9xx` store, and `mov dx,[pal_a]` ... `mov [pal_a],dx` is a `short` save of pal_a that
  DX keeps across the call (0x1ce57/0x1ce6c, 0x1d73e/0x1d758, 0x1e816/0x1e829). CONFIRMED by
  rebuild: switching `src/ww.c` to this prototype removed those rows in credits_screen, demo_mode,
  main_menu and 6 other functions (iter 36).

## `fn_36f55` 0x36f55 - the second map-mask writer: 0x6400 bytes to A000:7d00 — CONFIRMED (disasm)

- `pushal`/`popal`, no arguments, source `[0x7eb54]`: `mov edi,0xa0000` + `add di,0x7d00`
  (0x36f5e-0x36f63), `mov cx,0x6400` (0x36f68), then per source byte a write of `[edi]` plus
  `shl ah,1; cmp ah,8; jle loop` and, only when the rotated value passes 8, `inc di` and `ah=1`
  (0x36f79-0x36f86), with the sequencer map mask (port 0x3c4 index 2) re-programmed to `ah` at
  each iteration. So the mask runs 1, 2, 4, 8 and 25600 writes cover 6400 video
  addresses (A000:7d00..0xe0ff), each written once per plane: the source holds 4
  bytes per video address (CONFIRMED by the loop shape). `setpalette` programs
  `0x3ce ax=0x4105` (chain-4 write, line 12 above) and the game sets INT 10h
  `0x13` (`setvmode_keep(0x13)`, `src/ww.c:19453`), so what four map-mask writes at
  one chained address do is OPEN - see findings/formats.md ".PAR" and QUESTIONS 20.
- 0x7d00 = 32000 = the start of the third 0x3e80-byte page. The blob's pixel geometry is
  LIKELY a 320x20 band (findings/formats.md ".PAR"): `1.PAR` has 41 distinct byte values and
  its strongest byte-to-byte correlation is at lag 640 (mean |Δ| 2.8), with 1280 (a 320-pixel
  row at 4 bytes each) at 5.77 and lag 1 at 8.0.
- Callers pass `p6a980` and guard on `v7d998` being 0 or 3 (`src/ww.c:4416-4417` where `p6a980`
  holds the `<n>.PAR` blob, `src/ww.c:17259-17260`, `src/ww.c:17479-17480`): the image is
  blitted once at race start, not per frame.

## Traps

- A ternary is not the game's flip: `pal_a = pal_a ? 0 : 0x3e80;` selects the value in EAX and
  stores once, which no site in the game does. Write the two-branch `if`/`else` (see above);
  that is worth 71 aligned instructions in `cars_menu` alone.
- `setpalette` clobbers nothing the C sees (`pushal`/`popal`) but its `rep movsb` runs with all
  four write planes enabled: it is a whole-page copy, so calling it is never cheap (0x3e80
  bytes) — it does not belong inside a per-frame loop.
- The two helpers are *tail-call* / *call* variants: 0x1c068 ends `jmp setpalette` (no frame,
  nothing to restore), 0x1c0ac uses `call` because three stores follow. Watcom only emits the
  `jmp` when the callee follows immediately in the caller's tail — a port that writes
  `setpalette(); return;` in a frameless void function must produce `e9 ...`, and a
  near-miss `e8 ... c3` there is that difference, not a bug.

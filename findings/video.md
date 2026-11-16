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

## Traps

- `setpalette` clobbers nothing the C sees (`pushal`/`popal`) but its `rep movsb` runs with all
  four write planes enabled: it is a whole-page copy, so calling it is never cheap (0x3e80
  bytes) — it does not belong inside a per-frame loop.
- The two helpers are *tail-call* / *call* variants: 0x1c068 ends `jmp setpalette` (no frame,
  nothing to restore), 0x1c0ac uses `call` because three stores follow. Watcom only emits the
  `jmp` when the callee follows immediately in the caller's tail — a port that writes
  `setpalette(); return;` in a frameless void function must produce `e9 ...`, and a
  near-miss `e8 ... c3` there is that difference, not a bug.

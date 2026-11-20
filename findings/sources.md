# Outside sources: the author's own code and comments

What Wacky Wheels' programmer, Andy Edwardson, published about the game, and what each piece
confirms here. Levels as in METHOD.md section 2.

## The Dopefish source (CONFIRMED byte for byte)

Edwardson posted two functions of the game on the 3D Realms forums (thread t=2124, offline; not
in the Wayback Machine). They are mirrored, reformatted, on Phil's Wacky Wheels Site:
https://ctrl-alt-rees.com/archive/yellowantphil.com/wacky_wheels/code.html (mirror of
yellowantphil.com, page dated 20 Apr 2013).

- `load_dope` is our `ani_dope` 0x13780, `plot_dope` our `ani_draw` 0x13858.
- **CONFIRMED (iter 93p):** the posted text, pasted into a copy of `src/ww.c` in place of our two
  functions with `#define`s from his names to ours, compiles under Watcom 9.5b `-s -3r -oneatx`
  to the game's bytes. Both functions MATCH, and all 3,190 fixups name the same targets as in
  the game. This is the first check of the toolchain against the developer's own source, not
  against our reconstruction.
- His `load_dope` stores `dope_ptr`/`bubble_ptr` first; the binary stores them after the counters,
  and our matching version was written in the binary's order. Both orders compile to the same
  bytes: 9.5b reorders these independent stores to globals itself (CONFIRMED by that compile), so
  for stores like these the written order is no lever.
- Names it gives (all CONFIRMED, written into `analysis/ww_names.txt` as "original name"):

| Original | Ours | Original | Ours |
|---|---|---|---|
| `lib_find` | `datfind` | `dope_time` | `anistart` |
| `lib_read` | `readdat` | `tickframes` | `ticks2` |
| `gigdat` | `anidata` | `bsp` | `sprptr` |
| `my_voc` | `sfx2data` | `scx` / `scy` | `spr_x` / `spr_y` |
| `playeng` | `soundon` | `dep` (72, 15) | `spr_w` |
| `dope_loaded` | `anidope` | `wid` (80, 25) | `spr_h` |
| `dope_ptr` / `bubble_ptr` | `anip1` / `anip2` | `plotclip` | `drawsprite_clip` |
| `dope_stage` / `dope_frame` | `aniphase` / `aniframe` | `rtdif` | `since2` |
| `dope_x` / `dope_y` | `ani7ea98` / `ani7eb0c` | `play_my_voc` | `sfx_play2` |
| `bubble_on` / `bubble_x` / `bubble_y` | `ani7ea8c` / `ani7ea94` / `ani7ea9c` | `dopefish` | `anikick` |

  `gigdat` also appears in the game's own message "gigdat failed", logged when the `anidata`
  malloc (0xB6E0 bytes) fails. Our `spr_w` holds his `dep` and `spr_h` his `wid`, so his naming
  of the sprite's sides is the other way round from ours.

## The map renderer prototype (not the shipped code)

gamedev.net topic 51626 (June 2001, user ancientcoder = Edwardson; Wayback copy of 20 Apr 2008),
also on the code page above: `Draw_Map`, a 1993 floating-point C test of the floor renderer. He
says the shipped renderer was rewritten in assembler with fixed-point maths, so it is not in
`src/ww.c`: the floor is `floor_draw` 0x37e94 in `src/wwasm.asm`, three unrolled copies by detail level (findings/video.md), which fits his "unrolling the rendering loop helped out quite a bit". Design facts from the thread (STRONGLY
SUPPORTED: the author, years later; check each against the binary before using it):

- 32x32 tiles on a 1024x1024 fine coordinate grid; the map is a 2D array of tile pointers.
- Every kart sprite is 48x48, pre-scaled for several distances; the scale comes from the
  distance to the player (a square root).
- Collision: each ground tile has a mask tile of the same shape, a surface value per pixel
  (1 grass, 2 water, ...; 0 impassable), so one wheel can be on grass and the other on a wall.
- Computer karts follow connected line segments laid on the map, with crossover points to other
  lines; at set points each compares its race against the player's and turns more aggressive or
  slightly faster.
- Team: Edwardson (code, design, sound effects), Shaun Gadalla (art, tracks), Mark Klem (music);
  Jim Dosé's sound engine was bought in. Just under a year of work. Multiplayer was added late in
  beta "after Doom came out" and "nearly involved a rewrite".

## Other notes on that site

- A `/NOFUN` switch ("Searching for Epic games on your computer...") existed before release and
  was removed from the final version (history page; source: a 3D Realms forum thread).
- Copysoft's Skunny Kart was built on Wacky Wheels' source (history page).

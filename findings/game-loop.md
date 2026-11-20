# Race setup: the seat order and the per-player rows

INDEX question: "Startup chain, main loop, frame timing". Status: **OPEN** (only the two
setup helpers are known; the loop itself is not).

## `fn_1be3c` builds the start-seat order in `v70720[]` (0x1be3c-0x1beff)

Called from `fn_1e2e4` at 0x1e5b9 and from `fn_1f858` at 0x1ffee and 0x202c9 — i.e. once per
race start, never during a race. LIKELY (the calls, the stores and the compares are all in the
disassembly; only the *meaning* of the table is an inference):

| Step | Address |
|---|---|
| `srand(racetime < 8 ? 8 : racetime)` (clib `srand` 0x430ac) | 0x1be3f-0x1be4e |
| `v70720[0..7] = -1` (shorts, `movw $0xffff`) | 0x1be53-0x1be67 |
| `v70720[0] = myplayer` | 0x1be69-0x1be75 |
| if `twoplayer` or `splitscreen`: `v70720[1] = v88c6a`, start index 2 | 0x1be7c-0x1be9c |
| fill the remaining slots `cd = 1..7` (or 2..7): candidate = `rand()` in 0..7 when `splitscreen == 0`, else the previous candidate + 1 wrapping at 7 | 0x1bea1-0x1becf |
| reject a candidate that already appears in `v70720[0..7]` and retry | 0x1bed3-0x1bee0 |
| `v70720[cd++] = cand`; loop while `cd <= 7` | 0x1bee9-0x1bef9 |

So `v70720[]` is a permutation of the eight seat numbers 0..7 with the two local players pinned
first — a starting-grid order. It is read at 0x17273 (`mov dx,[edx*2+0x70720]` in `fn_17126`) and
written per player at 0x1fe78 (`fn_1f858`). The random draw is seeded from the elapsed
`racetime`, so with `racetime < 8` the grid is the same every time (0x1be44 clamps to 8).

Rebuild: `src/ww.c` `fn_1be3c`, near miss (196 bytes = the game's size, 10 instruction diffs;
see the `differs:` mark and `analysis/shapes/SHAPES.C` k1-k4).

The retry is per slot, CONFIRMED at the instruction level (iter 63): the game keeps one head for the
nested loops at 0x1beae, and the reject path jumps there (`je 0x1beae` 0x1bee0) over the
post-increment 0x1beec and the store 0x1beed, so `cd` is unchanged and that slot is drawn again; the
two range rejects of the candidate (0x1bec2 `jl 0x1beae`, 0x1bec8 `jg 0x1beae`) are the same head,
which re-reads `splitscreen` 0x1beae - dead, nothing between the test and the draw writes it.

**The draw rejects almost everything, so this function is dominated by `rand`** (CONFIRMED, iter 81).
The candidate must land in 0..7 as a short: `test ax,ax; jl 0x1beae` 0x1bec2 and `cmp ax,7;
jg 0x1beae` 0x1bec8, and `rand` 0x43088 returns `(seed >> 16) & 0x7fff` (0x430a2 `shr eax,0x10`,
0x430a5 `and eax,0x7fff`) - about one draw in 4096 is accepted. `hotfn.py` over `demo_mode`'s call at
0x1e5b9 counted 57134 draws at 0x1beb8 for 6 accepted seats, so a full grid costs the real game a few
tens of thousands of `rand` calls. `srand(racetime)` at 0x1be4e is a call, so under the emulator's
stub it seeds nothing and the sequence is whatever the image's static seed holds.

Emulator consequence (CONFIRMED, iter 81): with the call stub leaving EAX = 0 every draw is 0, the
repeat test 0x1bee0 `je 0x1beae` always fires, and `fn_1be3c` / `demo_mode` sit at the `CALLS` cap -
not a wait on a global, so `SEED=`/`TICK=` never reach it. `difffn.py RAND=8` (the stub draws the
image's own clib LCG, 0x43092 `imul edx,[eax],0x41c64e6d` + 0x43098 `add edx,0x3039`, mod 8) makes
both sides fill the table and return: `difffn.py fn_1be3c` prints **SAME with both sides returning**,
control `NEW=wworig MAP=.work/link/WWORIG.MAP RAND=8` SAME.

## `fn_1bf00` (0x1bf00-0x1bf7d) writes one row per player before the race

Called from `fn_16f90` at 0x17098. The stores are CONFIRMED, what the values mean is
SPECULATIVE:

```
n   = nplayers                                       ; 0x1bf04 (esi, restored at 0x1bf71)
cnt = v7eca4 + 1, clamped to 0x15                     ; 0x1bf0b-0x1bf19
for (i = 1; i <= n; i++)
    players[i].f64 = 1                                ; 0x1bf36 / 0x1bf5b
    players[i].face = (cnt-- > 0) ? v7ec6c : v7ec70   ; 0x1bf40 / 0x1bf65
nplayers = n                                          ; 0x1bf71
```

The record base is the one the rest of the port uses: `players` at 0x6aa90 with 0x68-byte
records (names line 281; pack(1) pinned by the dword at +0x5E, findings/executables.md), so
0x6aab6 = `players[i].face` (+0x26) and 0x6aaf4 = `players[i].f64` (+0x64). LIKELY.

`v7ec6c` and `v7ec70` are dwords everywhere else (multiplied by 0x98 = the size of a `faces`
record at 0x2b912, 0x2b950, 0x328a3) but are read as **words** here — so the low word is a
`face`-table index and the field `players[].face` (+0x26) is being assigned per player, the
first `v7eca4 + 1` players from one index and the rest from the other. `v7eca4` is incremented
by `fn_16eac` (0x16ef3) and cleared at 0x2fd8c: LIKELY a "how many players keep their face"
count. SPECULATIVE: a net/handicap rule for who gets the chosen car graphic.

Rebuild: `src/ww.c` `fn_1bf00`, near miss (97 of 125 bytes; the game keeps the store/continue
block inside both arms of the test, our build merges them — see the `differs:` mark).

## `fn_16f90` 0x16f90 sets `players[].f14` to the kart's offset from the start line (CONFIRMED)

The pre-race reset walks the 8 `players` rows (`si` = 0..7, record base 0x6aa90, stride 0x68) and
for each row stores

```
x        = order_a[si]                    ; 0x1716a -> +0x16
y        = order_b[si]                    ; 0x17179 -> +0x18
f10      = *0x72354 - abs(v88c06 - y)      ; 0x171ed-0x171f6, +0x10
f5e      = *0x72350 + abs(v88c06 - y) * 4  ; 0x17203-0x1720b, the `.POS` path cursor, +0x5e
f00      = x < v88c04 ? 3 : x > v88c04 ? 4 ; 0x17211-0x17240
f14      = abs(v88c04 - x)                 ; 0x171d0-0x171e5 (cdq/xor/sub abs) then 0x1727b
face     = v70720[si]                     ; 0x17273/0x17283, the seat `fn_1be3c` picked
```

CONFIRMED as the instruction sequence above; the values are CONFIRMED by running it: `tools/wc/difffn.py
fn_16f90` executes the game's 0x16f90 and our build's copy on their own images and the game leaves
20, 40, 60, 80, 60, 40, 20 at 0x6ab0c (stride 0x68, i.e. `players[1..7].f14`), a symmetric pyramid
around the middle seat.

What it is for: `cpu_drive` 0x28cac walks a kart along the `.POS` path through the `f5e` cursor and,
while `f00` is 3 or 4, adds `dir = -f14` (3, behind the line) or `+f14` (4, ahead) to `x` every step,
then `f14 -= 4` and `f00 = 0` when it runs out (src/ww.c:13272-13283) - so `f14` is the **remaining
offset from the start line**, and the karts slide into their slots over the first frames of the race.
SPECULATIVE: 4 pixels per frame at the pre-race tick rate.

Our build stored 0 there: `players[k].f14 = 0`, with the same `abs(v88c04 - x)` computed into `dx`
and thrown away - so `dir` was always 0 and every kart started exactly on the path. Fixed iter 69
(`players[k].f14 = dx`, src/ww.c:4194). Neither the row count nor `nmrefs.py` saw it: both sides
store to the same address with the same instruction, only the value differs.

## Hit tests: `kart_hit` 0x216a0 and `shot_hit` 0x218a4; `karts[]` 0x68410

- **`karts[]` at 0x68410 (0x104-byte records) is `cars[]` (0x68420) seen 0x10 bytes earlier**:
  STRONGLY SUPPORTED. `shot_hit` sets `curkart = 0x68410 + i*0x104` (0x21cde) and stops the
  engine sound at karts +0xd4 = 0x684e4, which is `cars[i].sndhandle` (+0xc4). `curkart` (0x7d5b0)
  points at one of these records. Both rebuilds use `struct kart` (pack(1), src/ww.c).
- Both functions call the asm map probe `f37b04` 0x37b04 with the point in `v7d7a4`/`v7d7a8`; it
  returns a class in `v7e568` (CONFIRMED by the code: 10 = pickup for a kart, 3 = v88bae 1).
  How it computes it (CONFIRMED, disasm 0x37b04-0x37bb0): both coordinates are clamped to
  0..0xfff (0x37b10-0x37b48), the 128x128 tile-pointer map `v7d6a4` is indexed at row `[y>>5]`,
  column `[x>>5]` (0x37b5c-0x37b72, `[0x7d7a8]` is the row), the tile pointer becomes a tile
  index by `- v7d64c` and `>> 0xa` (0x37b74-0x37b7c), the byte at
  `tile + 0x1b000 + (y&31)*32 + (x&31)` is read (0x37b8d-0x37b95 - the mask picture of that
  tile) and the `<n>.SIN` record of that tile index supplies dword 0 when the byte is 0, dword 1
  otherwise (0x37b9e / 0x37ba9) as `v7e568`, the index of the 0x12e-byte `surfs[]` record
  (src/ww.c:1425). Data layout: findings/formats.md, ".SIN - one 12-byte record per tile".
- The touch test is `|dx| + |dy|` (abs, `cdq/xor/sub`) as a short against 14 for entities and kart
  to player, 18 for a shot to a player (CONFIRMED, byte-identical / near-miss rebuilds).
- The result goes to `v88bae`: 1 map, 2 entity, 3 player, 4 pickup (CONFIRMED). The player loops
  run `i <= nplayers` (0x2189e, 0x21dd3: `jle`), one more than the count (LIKELY a game bug or a
  count stored as n-1).
- In `shot_hit`'s AI-hit path the taunt block (`voice_play(0xff)`, the `face == v7ec6c` test,
  `f66 = 1`, `f64--`) runs when `v7ec74` (netvoices) is **not** zero, i.e. in a network race
  (CONFIRMED: `cmp [v7ec74],0; je 0x21aee` at 0x21ab2-0x21ab9 skips it; the sibling test 0x21af7
  -0x21afe in the same function is `cmp; jne 0x21b0a` in both the game and our build, so the
  inverted sense at 0x21ab9 is source-level, not a compiler choice). There the follow-up
  `voice_play(0x11)` is gated on `v7ec74 == 0` (0x21af7).

- **`v7d874` (0x7d874) is the "current shot" pointer, and every read of it sits under a writer**
  (CONFIRMED, `tools/le/xref.py ww 0x7d874` over all 29 references): the only readers are
  `shot_step` 0x25e6c (8 reads) and `shot_hit` 0x218a4 (3 reads); `shot_hit` is called only from
  `shot_line` 0x22230 (0x22296, 0x222f3, 0x22328), `shot_line` only from `shot_step` 0x25fd6, and
  `shot_step` only from `shots_move` 0x2602c (call 0x26090 right after its `mov [v7d874],ecx`
  0x2608a) and from `shot_fire` 0x2d548 (call 0x2d5d3, store 0x2d5ad before it). So the value
  `shots_project` 0x2632c leaves behind is dead: the game steps its loop pointer past the table
  (`add ecx,0x22` 0x26684, `mov [v7d874],ecx` 0x26697 - the record after `shots[]` 0x7f2b0, i.e.
  shots + 61*0x22) while the rebuild leaves the last live shot. `shots[]` records are 0x22 bytes
  (`add ecx,0x22` 0x26684 in the loop tail of `shots_project`).

## Pickups, weapons and shot kinds: `SPRITE.ATR` kind -> `kart.f16` -> shot kind

STRONGLY SUPPORTED: read from the rebuilt C of three near misses whose addresses agree with the
game (nmrefs): `kart_bump` 0x22bf4 (pickup), `kart_fire` 0x24364 (fire), `shot_hit` 0x218a4 (hit).
What the sprites look like and what the player sees are not checked live.

- **Pickup** (`kart_bump`, the entity touched is `ents[CR->f22]`, its set's `kind` from
  `SPRITE.ATR` +0x00, findings/formats.md): kind 0 = an obstacle (one-player, off-line only, and only
  with `f42 >= 100` and `CR->f20 == 3`: crash, voice 0xd, engine stopped, `f7e = 1`); kind 1 (OB1) = a one-off effect (voice 0x16, `v88c8c = -1`, three `tb80ae8`
  rows 0xb apart; not in split screen); kind 2 (the 18 H sets) = **ammo**, `f6a += 4` up to
  0x63; kinds 3..7 (H1, H15, H12, H16, H20) = **a weapon**, `f16 = kind`, taken only when the
  kart holds no weapon (`f16 == 0`) and `f6a < 0x63`. A taken pickup is hidden (`ents[].f2 = -1`).
  In a network race kinds above 2 are all replaced by the first kind-2 set (`ents_flags`).
- **Fire** (`kart_fire`, fire key `joy88b42`, blocked while `players[].f12` is set): the weapon is
  `f16`, or with `f16 == 0` the ammo (`f6a--` per shot) and the `fe0` bits (4 -> 0, 0x10 -> 3,
  8 -> 8). Holding fire 20 `ticks2` sets weapon 8 (not in the shareware, `v7e574 == 0`).

  | weapon (`f16`) | shots | shot kind | placed | sprite | voice |
  |---|---|---|---|---|---|
  | 0 (ammo) | 1 | 0 | at the kart | `hm7d740` | 0x14 |
  | 3 (H1) | 3 (`v7eb9c` 0, 1, 2) | 0 | at the kart | `hm7d73c` | 0x13 |
  | 4 (H15) | 2 | 1 | `curpos` +-0x1e0 on the track (0x780 a lap) | `hm7d738` | 0x17 |
  | 5 (H12) | 1 | 2 | `curpos` + 0x3c0 (half a lap ahead) | `hm7d734` | 0x17 |
  | 6 (H16) | 1 | 3 | same | `hm7d730` | 0x17 |
  | 7 (H20) | 1 | 4 | same | `hm7d72c` | 0x17 |
  | 8 (held fire) | 1 | 5 | at the kart | `hm7d728` | 0x14 |

- **Hit** (`shot_hit`): the owner is immune to kinds 0 and 5 and to an armed shot (`f08`).
  A computer driver: kind 5 spins (`f58 = 2`, voice 0x19), any other kind slows it (`f58 = 1`,
  voice 0x11; in a network race the taunt path above). A human: in a two-player race kinds 0-2
  knock the kart out (`f02 = 2`, engine stopped); kind 4 sets `kart.f14`, which swaps the two
  inputs `key88cd5`/`key88cdd` (steer left / steer right: the state bytes of key slots 0 and 1,
  whose SETUP labels are "Steer left"/"Steer right", findings/formats.md) in `kart_frame`
  0x290a0 (**reversed steering**; who
  clears `f14` is not found in the C - its stamp `f8a` has no reader there, OPEN); kind 5 spins;
  the rest play the hit animation (`f1a = 7`, `tb7b448`). Kind 5 counts as kind 0 in two-player
  and network races.

## The entity iterator `v7d7f4` 0x7d7f4 (CONFIRMED)

- `ents[]` records are **0xc bytes** (`add ecx,0xc` at the `ents_project` loop tail 0x25e43; base
  0x73938, so `ents[8]` is 0x73938 + 8*0xc = 0x73998, findings/car_select.md). Fields: `id` +0,
  `f2` +2, `x` +4, `y` +6, `f8` +8, `frame` +0xa (0x25a9c-0x25ac3 reads them in that order, 0x25c82
  `movsx edi, word ptr [ecx + 0xa]`).
- The game walks the array through the **global** `v7d7f4`, never a local, in six functions:
  `ents_flags` 0x213f0, `kart_hit` 0x216a0, `shot_hit` 0x218a4, `kart_bump` 0x22bf4, `anim_tick`
  0x261fc and `ents_project` 0x25a78. A scan of the code object for the operand bytes `f4 d7 07`
  finds 23 references and they sit only in those six.
- Watcom keeps that global in ECX and materializes it where another path needs it: stores at
  0x25aab, 0x25ba3, 0x25e33 and the loop-exit path 0x25e5b, reloads after the `in_sector` 0x25b08
  and `isqrt` 0x25bc7 calls at 0x25b6d / 0x25bf6 / 0x25e3d. "Ours uses a local pointer and stores
  once per record" is therefore the same behaviour, and `nmrefs` reports the difference as
  `game more 6x 0x7d7f4`.
- **What `ents_project` leaves in `v7d7f4` is dead** - which is why it is safe for our rebuild to
  leave the last kept record where the game stores `ents + nents*0xc` (0x25e5b): every other reader
  *writes* the global before its first read (0x21483, 0x2176f, 0x218dc, 0x23234, 0x26317, all
  `mov dword ptr [0x7d7f4], ...`), and this function calls only `in_sector` and `isqrt`, which do
  not touch it. Method: prove a global's exit value dead by write-before-read in every reader, not
  by finding no reader.

## The kart animation state `kart.f18`: which table each `kart_frame` case reads (CONFIRMED)

`kart_frame` 0x290a0 ends in `switch (curkart->f18)`; its jump table sits in the gap in front of the
function at **0x29068** (14 entries) and is indexed **f18-1**: `mov bx,[edx+0x18]` 0x298cf,
`dec ebx` 0x298d3, `cmp bx,0xd; ja 0x29e01` 0x298d6. Every arm sets `kart.f4e` to
`&table[kart.f1a][v88b7e]` - row stride 8 (`lea ebx,[eax*8]`), index scale 4 - and which of the four
int tables an arm picks is what the animation *is* (the choice per case CONFIRMED from the
addresses, the meaning still LIKELY):

| f18 | arm | table (base load) | guard, from src |
|---|---|---|---|
| 1 | 0x298eb | `tb7b448` via the block 0x2993f, shared with case 2 | `f20` set = return; `--f1a > 6` 0x298ff, else reset |
| 2 | 0x2996e | `tb7b448` 0x29943 (same shared block) | `f20`; `++f1a < 6` 0x29982, else reset |
| 3 | 0x299ca | `tb7b4d8` 0x29a2a | `f3c`; `++f1a < 3` 0x299de, else reset |
| 4 | 0x29a55 | `tb7b4d8` 0x29ab5 | `f3c`; `--f1a > 3` 0x29a69, else reset |
| 5 | 0x29c59 | `tb7b518` 0x29c85 | `f1a` walked toward `f1c` 0x29c5d/0x29c69, `f18 = 0` when equal |
| 6 | 0x29ae0 | `tb7b418` 0x29b12 | `f1a == 0`: `ticks2 - f02 >= 3` 0x29b06 -> `f18 = 8` 0x29b08; else `--f1a == 0` -> `f02 = ticks2` |
| 7 | 0x29b43 | `tb7b418` 0x29b7a | `f1a == 4` -> `f18 = 9` 0x29b70; else `++f1a == 4` -> `f02 = ticks2` 0x29b5b |
| 8 | 0x29bab | `tb7b418` 0x29bff (index block shared with 9, its tail is the case-14 one at 0x29de5 - `jmp 0x29c04`) or reset -> `tb7b518` 0x29be0 | `++f1a < 2` |
| 9 | 0x29c09 | `tb7b418` via 0x29bfb (`jg` 0x29c12) or reset -> `tb7b518` 0x29c3e | `--f1a > 2` |
| 10 | 0x29cb6 | `tb7b378`, one int by `v88c5c` | voice pair + `++v88c5c == v88c52` |
| 11, 12 | 0x29e01 | none (default, the epilogue) | |
| 13 | 0x29d3d | `tb7b238`, one int by `v88c5a` | `++v88c5a == v88c50` |
| 14 | 0x29d8b | `tb7b448` 0x29de0 | `++f1a < 8` 0x29d94, else reset |

"reset" is one statement, `f00 = 0; f18 = 0; f1a = 2; f1c = 2; f4e = &tb7b518[2][v88b7e]`
(`ANIM_RESET()` in `src/ww.c`, 6 uses + 3 explicit sites = 9 sites): the game keeps **9 copies** of
its base load - 0x29265, 0x29912, 0x2998e, 0x299ea, 0x29a75, 0x29be0, 0x29c3e, 0x29c85, 0x29da0 -
where our build emits 5, so nmrefs' `game more 4x 0x7b518` and `5x v88b7e` are Watcom block merging,
not a missing reset (same for `anitimer`: 7 stores/reads in the game, 6 in ours, 0x294bc and 0x29543
merged).

## `main` 0x35ca4 ends in a tail call to clib `exit_`, not a return (CONFIRMED, iter 82)

- CONFIRMED (0x35e06 `call 0x341d8` (waitkey), 0x35e0b `mov eax,1`, 0x35e10 `jmp 0x42c11`; `exit_` is
  public at WWORIG.MAP `0001:00032c11` = 0x42c11 and in the hybrid map at `0001:0000c1d3`): the entry
  function's last act is a tail jump into the clib with EAX = 1, so a single-function run of `main` can
  only ever end at that call, never at a `ret` - and `waitkey` 0x341d8 is itself a tail call to `getch`
  (0x34202 `jmp 0x42c4d`). `tools/wc/difffn.py main` now ends the run at that call and prints **SAME,
  both sides stopping there, with equal stub-call counts (11 each)**: signal 0x43231 x2, 0x42826, 0x409ef,
  fputs 0x42bec x4, kbhit 0x42c36, getch 0x42c4d, exit 0x42c11 - our build's mirrors are 0x1c7f3 x2,
  0x1bde8, 0x19fb1, 0x1c1ae x4, 0x1c1f8, 0x1c20f, 0x1c1d3.
- CONFIRMED (the first bytes at 0x10000 in the game image, iter 82; the code object's base is 0x10000 and
  the data object's 0x60000, `.work/le/ww.json`): the image's code object starts with the Watcom
  filler `cc eb fd 90` (`int3`, `jmp 0x10000`, `nop`), the address a run lands on when a stub's faked
  return pops the wrong stack slot - it is not code and not a jump table.

## `race_run` 0x2fd8c leaves the race loop through the abort arm of `v7d998` (CONFIRMED, iter 83)

- CONFIRMED (whole-range disasm 0x2fd8c-0x327b3: nine `ret`s; the arm below is 0x31eae-0x31eca): the
  end-of-frame check 0x318ae-0x318c2 branches three ways on `v7d998` 0x7d998.
  - **0** -> 0x32056, keep racing (the loop head 0x3006a uses the same test at 0x3006a `cmp
    dword [0x7d998],0` to gate the per-frame array clear 0x30073-0x30095).
  - **1** -> the record-file arm: `call f2da28` 0x318c8, `mov eax,0x88cca` 0x318e4 + `call 0x429f9`
    (`fwrite`) 0x318e9, then 0x318ee `cmp byte [v88cec],0; je 0x32056`; when `v88cec` != 0: `v7d998 = 0`
    0x31900, `call 0x428b9` (`fclose`) 0x3190a, epilogue, `ret` 0x31918.
  - **any other value (2/3)** -> `jne 0x31eae`: 0x31eae `cmp byte [key88cd0],1` (ESC). Equal -> 0x31eb7
    `mov dword [v7ec68 0x7ec68],1` -> `add esp,0x44` + pop ebp/edi/esi/edx/ecx/ebx -> `ret` 0x31eca (this
    is what `demo_mode` 0x1e61a `cmp ebx,[v7ec68]` reads back). Not equal -> 0x31ecb `xor eax,eax` +
    `cmp dword [0x7d998],2` 0x31ecd, i.e. the two non-zero non-one modes diverge there; 2 then tests
    `key88cd2` 0x88cd2, `key88cd3` 0x88cd3, `v88cce` and `joy88b40` 0x31eda-0x31ef5.
- CONFIRMED (capstone scan of every instruction naming 0x7d998 in 0x10000-0x44000, iter 83): stores of 0
  at 0x1e69f, 0x1f896, 0x31900, 0x35d6b; of 1 at 0x34b0e; from a register at 0x1e485 (`demo_mode`
  0x1e2e4) and 0x34e5c (`intro` 0x348f4, which also reads it at 0x348fd). About sixty comparisons against
  0/1/2/3 sit elsewhere (0x13371, 0x18199, 0x3070d, 0x31ecd, 0x395f7, ...), so the abort arm is reachable
  only in the modes `intro` and `demo_mode` select. What 2 and 3 mean is **not** established here
  (SPECULATIVE: a per-player / two-player mode distinction).
- CONFIRMED (`SEED=0x7d998=2:d,0x88cd0=1:b tools/wc/difffn.py race_run` -> **SAME with both sides
  returning**; control `NEW=wworig MAP=.work/link/WWORIG.MAP` with the same env SAME; `hotfn.py` 91401
  insns at 2175 addrs and `returned`; address counts x1 at 0x318ae, 0x318bb, 0x31eae, 0x31eb7 and the
  `ret` 0x31eca, x0 at the three other returns probed (0x31608, 0x31918, 0x327b3), x1 at the loop head
  0x3006a - the run walks the race
  setup and one frame, and 2440 passes of `shots_project` 0x26344's 60-entry loop inside it): the seed
  pair is what ends the run. `EAX` on return is 1 in the game and 0 in our build, which carries nothing:
  `void FUN_0002fd8c(void)` and all four call sites in the decompile (lines 10774, 12023, 12119, 12183)
  call it as a statement.

## Traps

- The first race's grid is not one screen: under `tools/link/racetest.sh`'s fixed key path the
  ORIGINAL (`WWORIG.EXE`) drew grid A in 3 of 4 runs and grid B (other opponents, other order) in 1;
  our hybrid A 2, B 1 (iter 74). CONFIRMED by pixels (B-run of ours vs B-run of the original: mean
  0.0, 0.00% bad); the cause is LIKELY the `racetime` seed of `fn_1be3c` above, which wall-clock
  key timing moves. A wrong grid is therefore no evidence of a bug: compare with both references
  (`captures/race_play_ww.png`, `captures/race_play_ww_b.png`). There are more than two: a third
  grid C (another kart in slot 6) came from our hybrid under a slower driver script with the same
  keys (iter 74, `.work/link/race_grid_c_new.png`, 2.04% bad against B); the original was not run
  on that script, so "the original draws C too" is LIKELY (same seed path), not observed.
- `kart_draw` 0x24bcc reads its animation tables through two pointers that are zero until a race is
  set up, so a bare difffn run faults: `v7d89c` 0x7d89c is reloaded from `curkart->f76` at 0x24e97 and
  indexed `[v7d89c + track - 1]` at 0x24ef0 - with `curkart` 0x7d5b0 = 0 that is a read at 0xffffffff
  (`write_prot at 0xffffffff`), while every other `curkart->fXX` read is silent because difffn maps
  the null page. Seeding the null page's `f76` slot (`SEED=0x76=<ptr>:d`) plus `track` (0x7e728, an
  index into `k1[]`/`k2[]`, a byte index: 0x24ee5 loads it and 0x24ef0 adds it to the pointer) takes
  both builds to the return, SAME including the writes to the fake
  record (iter 76). The seed block has to be zero in both images - see GOTCHAS.
- The surface under a kart is not a lookup in the `.POS` map: `f37b04` goes through the `.M`
  tile map, and its transparent/solid test byte lives 0x1b000 bytes past the tile pointer - the
  second half of `v7d75c` (`add esi,0x1b000` 0x37b8f). Tiles are 32x32 with the pixel at
  `(y&31)*32 + (x&31)` (0x37b4d-0x37b5a).
- `fn_1bf00` starts at index **1**, not 0: `players[0]` is left alone (0x1bf1e `mov $1,%edx`).
- The clamp is on `v7eca4 + 1`, not on `v7eca4`: `cmp $0x15` after `inc ebx` (0x1bf12-0x1bf13),
  so at most 0x15 rows take `v7ec6c`.
- `shots_project` 0x2632c leaves `v7d874` pointing one record past `shots[]` (0x26697) - that is
  not an off-by-one to copy back: the pointer is only read under `shots_move`/`shot_fire`, which
  both store it before the call (see the `v7d874` bullet above). Adding `v7d874 = p;` after the
  loop to match costs rows for nothing (tried: 117 -> 120, `src/nearmiss.txt`).
- `ents_project` 0x25a78 leaves `v7d7f4` at the last record it drew where the game stores the
  past-the-end pointer (0x25e5b). Do not "fix" that: the value is dead, see the `v7d7f4` section
  above (same shape as the `v7d874` bullet).
- CONFIRMED (whole-module call-site scan; the next instruction at every site is a `cmp` of a global,
  never a read of EAX): `shots_project` 0x2632c returns a shot pointer in EAX that no caller uses -
  its six sites are 0x134ff, 0x13608, 0x289d7, 0x2f77d, 0x2fa18, 0x308d5 - and `players_ahead`
  0x34208 has one call site, 0x35115, which discards EAX too. A different EAX at return from those
  two is therefore not a behaviour difference (`tools/wc/difffn.py` prints an EAX note for both).
- `v70720` is `short`, not `int`: every compare and store in `fn_1be3c` is 16-bit
  (`cmp $7,%dx`, `movw $0xffff`), and -1 is used as the "empty seat" sentinel.
- A store can be at the right address with the wrong value and no comparison of *bytes* will see
  it: `players[].f14` was stored as 0 where the game stores `abs(v88c04 - x)` (0x1727b), one
  instruction both sides emit identically apart from the operand. `tools/wc/difffn.py` runs one
  function in the original and in the hybrid build and compares the memory they leave - it found
  this one; `nmrefs.py`'s address and constant censuses did not (iter 69).
- **A zero-argument run of a race-screen function returns at once and buys a worthless SAME**
  (CONFIRMED that the runs are thin, iter 85, `tools/wc/hotfn.py` walk counts at `CALLS=3000`,
  all of them printing `SAME` with both sides returning): `netkeys_in` 0x2d94c **29** insns at 29
  addrs, `kart_bump` 0x22bf4 32, `track_around` 0x283a4 44, `line_points` 0x21554 52.
  Each is a straight line into its own first branch, taken because this run leaves the globals it
  reads at 0 - and no conclusion about those four lines should be drawn from these runs (they need
  the race state seeded, the way `kart_draw` 0x24bcc's verdict line did). The four are read off and
  seeded in iter 86 (`track_around` 0x283a4 is MATCH now, so it is out of the list):
  - `kart_bump` 0x22bf4 has no single gate: its first branch is the **record count** `v88b78`
    0x88b78 (`cmp dx, word [0x88b78]; jg 0x22c7c` 0x22c2b, dx = 1 from 0x22c1d) over the `arr79a28`
    records at 0x79a28 + n*0x28, and behind it the per-record `f24` test
    (`cmp word [eax+0x24],0; jle 0x231d5` 0x22d5a), `players[CR->f18].f30[v88b7e]`
    (`cmp word [edx+ebx+0x30],0; je 0x22f2d` 0x22d88, players base 0x6aa90 stride 0x68 - the
    `imul edx,edx,0x68` + `mov ebx,0x6aa90` pair 0x22d64/0x22d67), `curkart->f7e` 0x22c81 with
    `v88c3c` 0x22c8c, the pickup gate CR->f1c 0x231db and `curkart->f7c` 0x231eb, and the entity
    kind word `sprattr[28*ents[CR->f22]].+0` (`mov bx,[ebx+0x72998]` 0x2322d,
    `test bx,bx; jle 0x2334d` 0x2323a). With
    `SEED=0x88b78=3:w,0x79a9c=1:w,0x6aac0=1:w,0x79ac4=0xffff:w,0x79abc=1:w,0x7d5b0=0x8200:d,0x827c=1:w,0x72998=1:w`
    the run is 336 insns at 276 addrs and SAME, control SAME (`.work/fncover.py`).
  - `kart_bump`'s pickup chain is entered through **`p7d86c` 0x7d86c, the current pickup record**
    (CONFIRMED iter 87: `mov edx,[0x7d86c]` 0x231d5 then `cmp word [edx+0x1c],0` 0x231db - with the
    pointer 0 that gate reads the DPMI scratch page, so an `SEED=` of the record fields at 0x79a28+n*0x28
    does nothing until the pointer is seeded too: `SEED=0x7d86c=0x79aa0:d` = record 3). Same for
    **`v7d800` 0x7d800**, the per-player pointer the f2c/f30/f34/f56 stores go through
    (`mov edx,[0x7d800]` 0x22ebc + `add eax,edx` with eax = `v88b7e`*2): `SEED=0x7d800=0x6aa90:d`
    moves those stores from page 0 into compared data. The voice/horn arm needs
    `v88c44` == 2 (0x22df1 `cmp word [0x88c44],2; jne 0x22eb5`); inside it `netvoices` 0x7ec74 picks
    `voice_check` 0x22e11 + `jmp 0x22eb0` 0x22e2d (network play) or, when `s7a9dc[pl->f26*0x18]` >= 4
    (0x22e56 `cmp dword [edx+0x7a9dc],4; jl 0x22eb0`), `sfx_busy` 0x22e5f + `sfx_play` 0x22e78 + the
    `s7b100`/`s7a9dc` reset 0x22e93/0x22ea4.
  - **The entity-kind dispatch has three arms, not the two iter 86 read off** (CONFIRMED iter 87, by
    taking each one): `test bx,bx; jle 0x2334d` 0x2323a is **kind <= 0** -> 0x2334d, `cmp bx,1;
    je 0x232d6` 0x23247 is kind == 1, and only kind >= 2 falls through to 0x2324d. `0x7d7f4` is not
    dead on this path: 0x23234 `mov [v7d7f4],edi` caches the `ents` record (base 0x73938, stride 0xc,
    from the record's `f22` index) that 0x2329c then writes (`mov word [eax+2],0xffff`), so with a real
    `ents` index the store is in compared data, not the page-0 scribble (findings/game-loop.md
    *Traps*, and GOTCHAS' dead-pointer list). Six seeded `difffn.py` runs, each SAME with both sides
    returning and the control SAME, cover kind >= 2 (377 insns at 301 addrs), kind <= 0 -> `0x233a4`
    -> `FX_StopSound` 0x233e0 (441/354), the `f24 > 0` tail 0x2345b-0x2369f (503/373), netvoices
    (455/347), the sfx arm (391/303) and kind == 1 (iter 86): 488 of the 606 instruction starts in
    0x22bf4-0x237a8 (`.work/fncov2.py`, capstone decode minus the union of the runs). Unwalked: the
    largest blocks 0x22fdf-0x2303d, 0x23600-0x23699, 0x2374c-0x2376c, 0x234b1-0x234d2, 0x22ef5-0x22f10,
    0x231b8-0x231cf (SPECULATIVE that they need a second player, `twoplayer`/`splitscreen` and real
    `faces[]`/spark records - each is behind such a test in the neighbours that *are* walked).
  - `line_points` 0x21554 is gated only by its own four input globals and its output cursor: the
    dx sign `test si,si; jge 0x2159e` 0x21590, the dy sign `cmp word [esp+0xc],0` 0x215a6, and
    `cmp si, word [esp+0xc]; jle 0x2160d` 0x215c4 (x-major loop 0x215dd vs y-major 0x2163d).
    `lx0` 0x88ba4, `lx1` 0x88ba6, `ly0` 0x88bc2, `ly1` 0x88bc4 and `linep` 0x7ed10 (seed to a scratch
    block, e.g. 0x8000, so the emitted point pairs land in compared memory): 3,40 / 5,27 is 495 insns
    at 57 addrs with 37 passes of the x-major loop, 40,10 / 60,5 is 721 insns at 69 addrs with 55
    passes of the y-major one; both SAME, control SAME, so the two Bresenham loops and both sign arms
    agree byte for byte in the output.
  - `netkeys_in` 0x2d94c was on the list by mistake: 0x2d94c-0x2da7 is 29 instructions and no branch,
    so the 29-insn run walks it all. What the zero run did not adjudicate were the seven masks, and
    `SEED=0x88cef=0x7f:b` does: the stores at 0x2d957/0x2d960/0x2d96d/0x2d97b/0x2d989/0x2d997/0x2d9a1
    write 1, 2, 4, 8, 0x10, 0x20, 0x40 in both builds (SAME, control SAME; EAX 0x40/0x2040 is the
    dead void return, GOTCHAS). A thin count is only a warning sign, not a verdict - check the
    function's own size first (`hotfn.py` insns vs the byte span).
  `minimap` 0x2a4ac belonged to that list at 15 insns until it was
  seeded: its gate is `v88ca0` (`cmp word [0x88ca0],0; je 0x2a60b` 0x2a4b8), and with
  `SEED=0x88ca0=1:w,0x88c40=1:w` (nplayers), `0x88c0c=2:w` (netgame) and `players[].x/y` seeded
  around the 0x7ff clamp (0x6aa90+0x16/0x18 and +0x68 for player 1) it runs 106 insns at 70 addrs and
  is SAME on both sides, control SAME - the two clamp arms and both lookup tables adjudicated
  (src/nearmiss.txt). Two more runs from the same sweep are thick enough to count: `leaderboard`
  0x2b270 (1673 insns at 304 addrs, SAME, both sides returning - it writes through its
  pointer-as-0 arguments into the compared zero page) and `time_str` 0x2a618 given a real buffer and
  a real count (`ARGS=<t>,0x8000`: 75 insns at 75 addrs = the whole fixed-width digit routine, and
  eight `t` values 0/59/60/3599/3600/4660/65535/100000 all SAME, control SAME).

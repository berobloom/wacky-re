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

Rebuild: `src/ww.c` `fn_1be3c`, near miss (196 bytes = the game's size, 13 instruction diffs;
see the `differs:` mark and `analysis/shapes/SHAPES.C` k1-k4).

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

## Hit tests: `kart_hit` 0x216a0 and `shot_hit` 0x218a4; `karts[]` 0x68410

- **`karts[]` at 0x68410 (0x104-byte records) is `cars[]` (0x68420) seen 0x10 bytes earlier**:
  STRONGLY SUPPORTED. `shot_hit` sets `curkart = 0x68410 + i*0x104` (0x21cde) and stops the
  engine sound at karts +0xd4 = 0x684e4, which is `cars[i].sndhandle` (+0xc4). `curkart` (0x7d5b0)
  points at one of these records. Both rebuilds use `struct kart` (pack(1), src/ww.c).
- Both functions call the asm map probe `f37b04` 0x37b04 with the point in `v7d7a4`/`v7d7a8`; it
  returns a class in `v7e568` (CONFIRMED by the code: 10 = pickup for a kart, 3 = v88bae 1).
- The touch test is `|dx| + |dy|` (abs, `cdq/xor/sub`) as a short against 14 for entities and kart
  to player, 18 for a shot to a player (CONFIRMED, byte-identical / near-miss rebuilds).
- The result goes to `v88bae`: 1 map, 2 entity, 3 player, 4 pickup (CONFIRMED). The player loops
  run `i <= nplayers` (0x2189e, 0x21dd3: `jle`), one more than the count (LIKELY a game bug or a
  count stored as n-1).

## Traps

- `fn_1bf00` starts at index **1**, not 0: `players[0]` is left alone (0x1bf1e `mov $1,%edx`).
- The clamp is on `v7eca4 + 1`, not on `v7eca4`: `cmp $0x15` after `inc ebx` (0x1bf12-0x1bf13),
  so at most 0x15 rows take `v7ec6c`.
- `v70720` is `short`, not `int`: every compare and store in `fn_1be3c` is 16-bit
  (`cmp $7,%dx`, `movw $0xffff`), and -1 is used as the "empty seat" sentinel.

# The game's compiler: Watcom C 9.5b's WCC386 from the inside

Why: the near misses left in `src/ww.c` come down to the code generator's register and slot
choices (GOTCHAS "coin flip"). Open Watcom's source gives the rules but not 9.5b's tie-breaks
(Open Watcom 2.0 builds 19 of the 205 functions 9.5b matches), so this file reads and traces the
real 9.5b compiler binary. Tools: `tools/wcc95/` (setup.sh, sweep.py, ratrace.py, raread.py).

## The binary

| What | Value | Level, evidence |
|---|---|---|
| File | `.work/toolchains/inst/WC95B/BIN/WCC386.EXE`, 628,178 bytes, sha256 `5e6a7b26b2016eb9...` (9.5 + patches A, B) | CONFIRMED |
| Format | MZ stub (0x2C524 bytes, has DOS/4G and Phar Lap strings), then a Phar Lap **P3** flat image: level 1, header 0x180, load image at P3+0x200, 0x6CE96 bytes, no relocations, not packed | CONFIRMED: header fields |
| Addresses | image linear base 0x1000 (P3 "base load offset"); entry 0x5F5F0 = `jmp` over "WATCOM C 386 Run-Time system..." | CONFIRMED: the jump and string |
| At run time (this DOSBox-X, `memsize=16`) | linear = image address + **0x9A0000**, one flat base for code and data (cs 0x0C, ds/ss 0x14) | CONFIRMED: halted mid-compile, bytes at EIP equal the image's. A memory search also finds a stale copy of the file at 0x54387: not the running code |

## Register allocator: routines (image addresses; Open Watcom names where the code matches)

| Address | Open Watcom | What | Level |
|---|---|---|---|
| 0x3D387 | `SortList(list, next_off, before)` | counts, then 0x3D3C5 `DoSortList` | CONFIRMED by disasm |
| 0x3D51F | `ShellSort` | gap = gap/2 + adjust, adjust alternating from 1; swaps when `before(a[i+gap], a[i])`; repeats a pass while it swapped; until gap 1. **Not stable**: ties end up where the gaps leave them | CONFIRMED: same as OW `cgsrtlst.c` |
| 0x3D4C2 / 0x3D48D | `MergeList` / `BuildList` | (MergeList only when the array alloc fails) | CONFIRMED |
| 0x2DEC2 | `AllocBefore` | sorts the temp list 0x6FF56: const temps last, then conflict savings (**unsigned**, `seta`), then temp id (+0x24) | CONFIRMED |
| 0x2DFA4 | (`RoughSortTemps`' SortList call) | | CONFIRMED |
| 0x44AAB | `ConfBefore` | conflict savings, **unsigned** (`seta`; OW compares signed) | CONFIRMED |
| 0x44AB5 | `SortConflicts` | sorts `ConfList` 0x6FEE4 | CONFIRMED |
| 0x44A29.. | `AssignConflicts` (second loop at 0x44A57) | per conflict in sorted order: on hold (+0x54 & 4) skipped; savings 0 -> `InMemory` 0x44621, else `GiveRegister` 0x444AA | CONFIRMED |
| 0x444AA | `GiveRegister` | calls `GiveBestReg` 0x440B4; its result (EAX) at 0x444DD | CONFIRMED |
| 0x46208, 0x3E716, 0x57A28 | other SortList users (temps by id; temps by flags/+8/+0x24/+0x10; a list at 0x7072C) | | LIKELY |

## Data structures (9.5b offsets)

- **conflict node**: +0 next, +4 next for name, +8 name, +0xC start block, +0x10 reg tree,
  +0x14 savings (unsigned), +0x54 state (bit 2 = on hold). CONFIRMED (code reads, traces).
- **name**: +4 class (1 memory, 2 temp), +0xC conflict, +0x14 front-end symbol handle (0 for
  compiler temps), +0x24 temp id, +0x2D temp flags (bit 0 = const temp). CONFIRMED.
- **block** +4 -> its first instruction; **instruction** +0x20 = source line (with `-d1`).
  CONFIRMED (probe T2: 274 = the first statement line).
- **register sets** (GiveBestReg's result): `01000003` EAX, `0200000C` EBX, `04000030` ECX,
  `080000C0` EDX (CONFIRMED against the probe's code); `00000003` AX, `000000C0` DX,
  `10000100` ESI, `20000200` EDI, `00000000` none (LIKELY: the pattern; 16-bit variables get them).
- **front-end symbols** (the C front end, not the code generator): 0x2E-byte records in handle
  order, name pointer at +0, the symbol's own handle at +0xC for locals (0 for parameters,
  whose handle is their position). CONFIRMED: probe T3 (`qqgamma` 0x18, `qqdelta` 0x19) and
  `net_recv` in the full build (`buf` 0xA56 .. `k` 0xA5D in declaration order).

## Facts this settles

- `-d1` changes no code: the full build gives the same 205 matches and identical differences
  (iter 93q), so traces use `-d1` to map events to source lines.
- The order of equal-savings conflicts is decided by the unstable Shell sort over the list's
  order before the sort. A "coin flip" is reproducible: given the pre-sort order and the
  savings, the outcome is computed, not random.
- A conflict's savings is a weighted use count (uses in loops weigh much more): `net_recv`'s
  nodes run from 168 to 10,408.

## First trace: net_recv 0x2DB00 (near miss, `crc` in EAX, the game ECX)

`ratrace.py WW.C out 15668-15719` (the whole file compiles, events of these lines are logged;
about 10 minutes): three allocation passes (41, 23, 20 conflicts). `crc` (`unsigned short`,
handle 0xA59, temp id 6, savings 4332) is given AX, 12th in order, after `n` took ECX. Open:
why AX scores best for it (GiveBestReg's per-register move counts, `CountRegMoves`), and what
in the game's source made CX win. Next probe: the scores inside GiveBestReg 0x440B4.

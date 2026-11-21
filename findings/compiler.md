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

| 0x440B4 | `GiveBestReg` | candidate loop 0x44169: for each register in the type's list (int: EAX, EDX, EBX, ECX, ESI, EDI, EBP; 16-bit: AX, DX, BX, CX, SI, DI) not in the conflict's interference set (+0x20) nor the except set: `TooGreedy` 0x4478A (result 0x441A3), then `CountRegMoves` 0x44277 (score at 0x441BB); best = higher score, on a tie a register already handed out (set 0x6FF14) beats one not; else the first in list order | CONFIRMED by disasm, traced |
| 0x2C3D5 | (`AddConflictNode`'s end) | a new conflict goes to the FRONT of `ConfList` | CONFIRMED |
| 0x32A28 | (unnamed) | moves the conflicts whose name has +0x18 bit 1 set to the front of `ConfList`, keeping their order | CONFIRMED by disasm |

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

## The allocation order is computed, not chosen (iter 93s)

`tools/wcc95/shellsort.py` simulates 9.5b's sort over a trace's pre-sort list: in all 15 passes
of the ten traced near misses (net_recv, pcx_load, netkeys_out/pack/in, shots_clear,
drawsprite_clip, self_project, voices_reset, fn_1bf00) the simulated order equals the traced
allocation order. CONFIRMED. So a "coin flip" between two locals is: equal savings, and the
Shell sort over their pre-sort positions puts one first; the first takes the first free register
in list order when every candidate scores 0.

**pcx_load 0x27060** (n and size swap ESI/EDI): `size` (handle 0x8BB) and `n` (0x8B8) both have
savings 24 and score 0 everywhere; `size` (pre-sort position 9 of 15) is sorted before `n` (13)
and takes ESI. The game's order needs one of: n's savings >= 25, size's <= 23, or n at a
pre-sort position 0-6, 8, 10 or 12 (simulated). The probe `.work/wcc/probe/PL.C` (pcx_load alone)
gives the same 5 rows and the same trace in 8 s; ten more spellings (`tools/wcc95/vbatch.py`)
changed nothing. Open: what in the source sets the pre-sort position (the order conflicts are
created) and the savings (IR references, weighted by loop depth).

**net_recv 0x2DB00**: not a tie. `crc` (temp id6) scores AX 8, CX 0 (DX, BX interfere), so AX
wins; our build also keeps a second 16-bit temp (id25, DX) as crc's copy for the compare after
the loop, which the game does not have (its crc is CX from the zeroing to the compare). The
difference is the split, so the source shape around the loop, not the register order.

**netkeys_out/pack/in (fixed, iter 93s)**: the one-local functions (`unsigned char c` in AL, the
game DL with `push edx`) were no tie of locals: the trace has a single temp (`c`, savings 18). The
game's DL is the code generator's temp for a global used in place: writing the body on `v88cf0`
(and `v88cef & N` per statement in netkeys_in) builds all three byte-identical (208 matches).
CONFIRMED by the full check.

**shots_clear 0x2D4FC (fixed, iter 94)**: the same lever one step further - a global that is a
POINTER walked in place. `v7d874 = shots; ... (v7d874++)->f04 = 0;` with no `struct shot *p` local
MATCHes: the game's EBX is the cached global (`mov ebx,0x7f2b0` 0x2d4fe, one store 0x2d53c) and the
local had handed EBX to the loop counter. CONFIRMED by the full check (211).

**voices_reset 0x11ADC**: not source order. The -1 and 0 of the unrolled voices loop are two constant
pseudos; ours gives EBX to -1 and EDX to 0 whichever store is written first - the `swap-stores`
whole-file variant (`.work/vbatch/V.OBJ`) leaves the rows at 0x11b1e/0x11b23 unchanged. So the
pre-sort position of a materialised constant is not the source order of its stores. CONFIRMED for our
build (that one experiment); the game's EDX/EBX stays unexplained, and no spelling of the two loops
(7 tried) moves it.

**shots_move 0x2602C**: the *form* of the use does move a walked global's home. `*v7d690++` keeps the
pointer in EDX and loads through `[edx]`/`[edx+4]`; the indexed pair `vec_x = v7d690[0];
vec_y = v7d690[1]; v7d690++;` puts it in EBX like the game (12 -> 7 rows, whole check unchanged) and
leaves only the p+1 copy, `lea eax,[ebx+4]` 0x26136 vs the game's `mov edx,ebx` + `add edx,4`
0x2612e/0x26136. CONFIRMED that the form changes the home; STRONGLY SUPPORTED that the game's C used
the indexed pair (its two loads go through two registers, 0x26139/0x26141).

CONFIRMED (iter 95, three probe compiles in `.work/vbatch/`, sha256 over each whole function's masked
bytes): the operand
order and parentheses of a flat sum never reach 9.5b's codegen. `(spr_w - 1) + spr_y`,
`spr_w - 1 + spr_y` and `spr_y + (spr_w - 1)` in self_project 0x26a3c give the same 1160 bytes
(sha `693313ad...`) and the same 3 rows, so which addend the allocator gives EAX cannot be steered by
spelling the expression differently - only by changing what the temporaries ARE (widths, locals vs
globals, how many are live). Equally negative: the game's `mov [0x7d690],ebx` 0x26130 +
`mov edx,ebx`/`add edx,4` 0x2612e/0x26136 + `mov edx,[edx]` 0x26141 (shots_move) was not produced by
any of the eight spellings tried in `.work/vbatch/shots_move.py`, including the four that read the pair through the incremented pointer like the
machine code does - they score 13 rows against the indexed pair's 7 (`.work/vbatch/shots_move.py`).

### iter 96: deleting the local shows whether a register is a home or 9.5b's temp (CONFIRMED, two deletes)

The cheapest test of "is this differing register a declared local's home, or the compiler's own
temporary" is one compile with the local declared nowhere and the global used in place; the row
survives only in the second case.

- `com_wait_byte` 0x2df70, `d` gone (`if (ticks - t0 >= 0x110)`): still `mov eax,[0x7e720]` +
  `sub eax,edx` + `cmp eax,0x110`, same 152 bytes, same 8 rows, and esc/t0 keep EBX/EDX through
  11 spellings (`.work/vbatch/com_wait_byte.py`, `cwb_nod.py`). That EAX/ECX row is a tie-break,
  not a home.
- `fn_1bf00` 0x1bf00, `val` gone (`players[i].face = v7ec6c;` in both arms): a fifth `push edi`,
  128 -> 136 bytes, `n` displaced into EDI, 9 rows -> 15 (`.work/vbatch/fn_1bf00.py`). The face
  value's register is a declared local in the game as well.
- One more fused form (STRONGLY SUPPORTED: 7 spellings, `.work/vbatch/net_input.py`): 9.5b
  compiles `t = g & m` as one load plus one `and` in one register, so the game's four grouped
  `mov dh/bl/dl/ah,[0x88cef]` (0x2f60b-0x2f61d) above four `and`s (0x2f623-0x2f62c) are not four
  `t = g;` statements - the split form makes every copy a live value of its own, +20 bytes of
  spills and 8 rows -> 31. Loads grouped above their masks are scheduling.

## First trace: net_recv 0x2DB00 (near miss, `crc` in EAX, the game ECX)

`ratrace.py WW.C out 15668-15719` (the whole file compiles, events of these lines are logged;
about 10 minutes): three allocation passes (41, 23, 20 conflicts). `crc` (`unsigned short`,
handle 0xA59, temp id 6, savings 4332) is given AX, 12th in order, after `n` took ECX. Open:
why AX scores best for it (GiveBestReg's per-register move counts, `CountRegMoves`), and what
in the game's source made CX win. Next probe: the scores inside GiveBestReg 0x440B4.

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

### iter 97: the form of the loop, and a merged local, both move homes (CONFIRMED, two whole-file experiments)

- `fade_in` 0x32cf8: `for (i = 0, k = 0; i <= 0x2ff; i++) { cur[i] = 0; if (++k > 2) k = 0; }` gives
  i EBX / k EDX; `k = 0; i = 0; do { cur[i] = 0; if (++k > 2) k = 0; i++; } while (i <= 0x2ff);` gives
  the game's i EDX (`movsx eax,dx` 0x32d20) and k EBX (`inc ebx` 0x32d23) - 13 -> 2 rows - and writing
  `k = 0;` before `i = 0;` fixes the last pair (`xor ebx,ebx` before `xor edx,edx`, 0x32d1c/0x32d1e):
  0 rows. The same initialisers in a `while` loop score 13/14, so it is the `do` form and not the
  order of the two assignments alone (`.work/vbatch/fade_in.py`, `fade_in2.py`).
- `fade_in` again, scheduling (CONFIRMED): `palptr = (char *)cur;` written above `dac_count = 0x100;`
  puts the frame-address load `mov eax,esp` at 0x32d38, above the two global stores, as the game has
  it (2 rows of the 13; 13 -> 12 on its own).
- `com_expect` 0x2e008: ONE local for the `com_getc` result and the return code (no `r`) flips two
  other homes - want ESI -> EBX (0x2e00b `mov ebx,eax`) and t0 EBX -> ESI (0x2e016) - 8 -> 4 rows,
  whole check unchanged at 211. `t0` deleted in favour of the global `v7ea68` it is stored into
  scores the same 4 (so t0 is a real local), and an added `d` for `ticks - t0` in four types and
  three declaration positions never moves the last 4 rows (`ticks - t0` in EAX here, the game's
  ECX at 0x2e08e).

### iter 98: delete-test on a tie, and the written order of switch arms is compiled (CONFIRMED, three vbatch runs)

- `pcx_load` 0x27060 (5 rows, `n`/`size` swap ESI/EDI): deleting either local costs rows - `t` gone
  (`fseek(datfile, ftell(datfile), SEEK_SET)`) 5 -> 11, the size expression inline in the `fread`
  argument 14, both gone 14, `t` reused as size 12, size computed before `ftell` 11, a
  `char *dst = p6a978s[n]` above `datfind` 9, `int t` and `unsigned long size` tie at 5
  (`.work/vbatch/pcx_load.py`). Both are homes, not 9.5b temps, so iter 96's delete test says
  "real local" here too; the ESI/EDI colouring stands.
- `net_recv` 0x2db00 (4 rows, `crc` EAX vs the game's ECX): `p` declared with its initialiser,
  `unsigned char *p`, an explicit `got = crc` before the compare, `int n, t, c, k;` split into four
  declarations, and `crc` declared before `rcrc` all tie at 4; `p` assigned late (before the byte
  stores) 15, `short crc` 13, `unsigned crc` 9 (`.work/vbatch/net_recv.py`). Declaration position
  does not reach this home either - consistent with the iter 93 trace reading (crc is not in a tie).
- `fn_19db8` 0x19db8 (18 rows): the written ORDER of a switch's case arms is compiled, not
  canonical. Moving the `case 4:` arm of `switch (v88bb2)` to the end costs 18 -> 32 rows, moving it
  second 32, and moving `case 3:` first 49, same body, same jump table
  (the table is indexed by case value, so only the emitted arm order moves).
  `.work/vbatch/fn_19db8.py`. So arm order is a real lever on switch-shaped near misses, and the
  written 4,1,2,3 is the closest of the four tried - do not "tidy" case labels into numeric order.
  Caching `v7ee5a` in a local above the switch: 26 rows; `switch ((unsigned short)v88bb2)`: 18.

### iter 99: the compare-destination rule survives the whole file; `register` is not a lever (CONFIRMED, two vbatch runs)

- `car_laps` 0x12ee0 (2 rows: ours `cmp eax,edi; jle`, the game `cmp edi,eax; jge`, same two loads and
  the same `add eax,edx`): the rule was previously read off probe compiles, which GOTCHAS says do not
  rank every function, so iter 99 re-ran it whole-file (`.work/vbatch/car_laps.py`). The probe's count
  reproduces exactly: `players[i].lap < nlaps + 1`, `players[i].lap < (nlaps + 1)` and
  `!(players[i].lap >= nlaps + 1)` all emit the game's `cmp edi,eax` + `jge` and cost 4 rows (the two
  `movsx` homes and the `add` move), every sum-left form keeps the game's homes and emits our own
  `cmp`/`jle` (2). A `short cap = nlaps + 1` local compared on the left 17, a `short lp` copy of the
  lap 21, `while (nlaps + 1 > lap)` 80. CONFIRMED: with two loaded operands, 9.5b's choice of `cmp`
  destination and of the first-loaded home are one decision tied to the source comparison's left side,
  so a 2-row compare-direction pair is unreachable by spelling (DREA harness/TRAPS.md "Watcom gives a
  comparison's source-LEFT operand both the `cmp` destination and the first register home").
- `net_recv` 0x2db00 (4 rows, `crc` home EAX vs the game's ECX; `.work/vbatch/net_recv2.py`):
  `register unsigned short crc` 4 and `crc` declared after `int n, t, c, k;` 4 - neither the keyword
  nor the declaration position biases a home (as iter 98 found for the split declarations);
  `rcrc != crc` is **byte-identical** to `crc != rcrc` - when one side of an `!=` is a stack slot,
  9.5b compares register-with-memory whichever way the C is written, so operand order there is not
  even a lever; `register int crc` 9; `unsigned short crc = 0;` in the declaration with the
  statement deleted 21; a hand-unrolled `for (k = 0; k < 4; k += 2)` with both calls written out 8,
  i.e. the game's x2 unroll of the crc loop (0x2dbb4-0x2dbd5: two calls, `add ebx,8`,
  `cmp ebx,0x10`, `jne`) is 9.5b's own expansion of a `k++` loop and a source-level unroll of the
  same two calls does not reproduce it.

### iter 100: a local nobody reads can still move the scheduler (CONFIRMED, one vbatch run, 15 spellings, one byte-identical)

- `line_draw` 0x21f04 (2 rows: the game reads err's initial value above the register rotation,
  `movsx edx,cx` 0x21fad then `mov eax,ebx; mov ebx,ecx; mov ecx,eax` 0x21fb0-0x21fb4; we read it
  below, `movsx edx,bx` 0x21fb3, `sar edx,1` at the same relative late slot 0x21fc2). Fifteen
  whole-file spellings (`.work/vbatch/line_draw.py`, `.work/vbatch/line_draw2.py`, control 2):
  `err = dy >> 1;` first with the current single rotation temp 9, with comma-form rotations 9, read
  between the rotation moves 19, with the two rotations in the other order 13, `err = dy; ... err >>=
  1;` 17, `short err` declared first 17, `short dy` first 2 (no code change - declaration order is
  not a lever here, as iter 99 found) and a second rotation temp alone 7. Only the combination
  **`err = dy >> 1;` first AND a second temp `u` for the sx/sy rotation** is byte-identical (0).
  CONFIRMED: with one temp shared by both rotations 9.5b sinks the load below the rotation, where the
  same value sits in the post-rotation register; the extra temp (never read afterwards) is what puts
  the load back above it. So "delete the local and see whether the row survives" (iter 96) answers
  *whose temp the register is*, not whether the original source declared it - a local that only
  changes the schedule of another instruction is real (GOTCHAS "Which of two locals gets EAX", DREA
  harness/TRAPS.md "Score a candidate only after checking which value it reads").
- The other side of that, same iteration (`.work/vbatch/music_jingle.py`, control 2 rows): the sunk
  zero argument of an inlined call is 9.5b's own def and cannot be pulled up by what the caller
  writes - `music_start(song, 0)`, `int loop = 0; music_start(song, loop);`, `music_start(song, song &&
  0)` and `music_start(song, 2 - 2)` compile byte-identical (116 bytes, `xor edx,edx` at 0x11893 in
  all four), so the game's early `xor edx,edx` 0x11885 is not evidence of a non-constant in its
  source. CONFIRMED: constant folding happens before that scheduling decision, so "make the constant
  non-constant" is not a lever.

### iter 101: three one-instruction schedule diffs are closed classes (CONFIRMED, three vbatch runs, 27 spellings)

The register-home list of iters 95-100 is exhausted (every name on it is MATCH or VB-marked), so the
same whole-file method went to the next 2-3 row lines of `src/nearmiss.txt`, none of which had had a
vbatch run:

- `track_setup` 0x124e8 (2 rows: the game loads `mov edx,[trk7ec44]` 0x124ea, `mov ebx,[trk7ebc8]`
  0x124f0, `mov eax,[track]` 0x124f6; ours puts the `[track]` load in the middle). 9 spellings: 7 are
  byte-identical to the control (the comma statement, `int x = trk7ebc8, y = trk7ec44;`, two-step
  copies through a third local, an unused third local, two case-label orders); the two that change
  anything (`int y, x;` with the statements reversed, `int y = trk7ec44, x = trk7ebc8;`) are worse at
  12 rows, and what they change is the **home** - which of EBX/EDX carries x - not the order.
  CONFIRMED with iter 99 (`car_laps`, `net_recv`): the emission order of independent global loads in
  one straight-line block is not a source property in 9.5b; declaration order is a source property of
  the register home only.
- `fn_182d0` 0x182d0 (3 rows, 1 real: the game's dead `mov esi,ebx` 0x182d4 immediately before
  `imul esi,ebx,3` 0x182d6 - a copy of the argument in EBX into the short local's home ESI whose
  multiply is emitted from EBX anyway). 9 whole-file spellings (`lim = a3; lim *= 3;`,
  `lim = lim * 3;`, `(short)(a3 * 3)`, three declaration orders, `int a3`, `register short lim`,
  `long t = a3; short lim = t * 3;`) all give 88 bytes / 3 rows. CONFIRMED: 9.5b re-materialises the
  `imul r,r,imm` from the argument register and deletes the copy any copy-then-multiply spelling
  produces, in the full build exactly as in a probe - so the iter 21 "probes do not rank this
  function" caveat does not explain iter 16's probe, and a dead register copy in the game's bytes is
  evidence of a local the port does not model, not of a spelling (QUESTIONS 28).
- `shot_hit` 0x218a4 (2 rows: the game hoists the karts index conversion `movsx edx,cx` to 0x21be1,
  above the `f02 = 2` store 0x21be4; ours emits it inside the `soundon` arm at 0x21bf0). Every
  register/array lever costs: `struct kart *k = &karts[i]` at the game's slot, after the call, or for
  the whole arm = 52 / 57 / 79 rows, because a shared pointer deletes the game's *second*
  `movsx edx,cx` at 0x21c16; `(char *)karts + i * 0x104` byte arithmetic 25; store order 4-16;
  `int flag` 6 (cause 1's home). An `int` local for the sign-extended index is byte-identical to the
  control, so no local owns that `movsx` and there is no home to give it. CONFIRMED (negative).
- Side product, `duck_screen` 0x2b5e8 (2 rows, no vbatch run): dumping both sides shows the *other*
  inlined `music_start` in the same function (0x2badc-0x2baf3, second argument 1) byte-identical, so
  iter 100's arg-def/store sink is specific to the `0` argument's fold. CONFIRMED by the bytes.

### iter 105: pcx_load's list is fixed by the front end; the aim is known, the C is not (CONFIRMED, 25 traced variants)

`tools/wcc95/flipsim.py LOG A B` (new) says what change to a traced pre-sort list would let conflict A
be allocated before B. For pcx_load 0x27060 (`n` id2 at position 13, `size` id3 at 9, both s24) it
reproduces the hand result above and adds the rest: n's savings +1 or more, or size's -1 or less; one
extra conflict at positions 2..9 (s24 or more) or 3..4/6..7/9 (any savings); or id27 (position 10, the
last `fread`'s temp), id41 or id33 removed. The parameters' conflicts are created first (both in the
global-bits phase) and every later one is pushed in front, so `n` and `name` are always the last two
(OW `dataflo.c` AssignGlobalBits, `conflict.c` AddConflictNode).
Traced with `vtrace.py` (9 s each): 25 spellings - casts on every argument, `*(p + n)`, `fseek(.., 0)`
/ `0L`, `&hdr`, `&hdr[0]`, `128`, `896L`, `(long)` / `+ -0x380`, `(int)datidx`, `(void)datfind`,
`return;`, K&R parameters in both orders, `size` in an inner block, the whole body in an inner block
and in `do { } while (0)`, `hdr` declared last - all give the **same 15 conflicts, positions and
savings**; only `size = ...; size -= 0x380;` (size s40, 9 rows) and size computed after the fseek
(12 rows) move anything, both the wrong way. So 9.5b's front end folds every spelling of this body to
one IR, and the game's must differ in something that is not a spelling of these statements (an
operation the optimizer removes only after allocation, other types in the externs, or other compile
options). The guided search works as a filter - one trace says whether a variant reaches the list at
all - but it cannot produce the variant.

## First trace: net_recv 0x2DB00 (near miss, `crc` in EAX, the game ECX)

`ratrace.py WW.C out 15668-15719` (the whole file compiles, events of these lines are logged;
about 10 minutes): three allocation passes (41, 23, 20 conflicts). `crc` (`unsigned short`,
handle 0xA59, temp id 6, savings 4332) is given AX, 12th in order, after `n` took ECX. Open:
why AX scores best for it (GiveBestReg's per-register move counts, `CountRegMoves`), and what
in the game's source made CX win. Next probe: the scores inside GiveBestReg 0x440B4.

# GOTCHAS: read every session

The traps that cost the most time, in one screen. The full list, by topic, is
`harness/TRAPS.md`: grep it when something looks impossible.

Rules for this file: one line or two per trap; harness, tool and process traps
only (game traps go in the findings file's `## Traps`); delete an entry when it
stops being true. Mark an entry `[generic]` when it is not about this game, so
it can be copied back into DREA.

## The ones that waste whole sessions

1. **Address arithmetic.** Real-mode linear = seg × 16 + off (one hex digit,
   0x824 × 16 = 0x8240). Compute with `python3 -c`, never in your head. Above
   0xFFFFF is your error, not the emulator's.
2. **A breakpoint that never fires** is far more often a wrong address than
   code that never runs. Recompute before concluding anything.
3. **gdb `eip` is always linear** (the stub adds the CS base), after any
   stop: never add CS × 16; offset = eip − CS × 16. The old rule "an offset
   after an interrupt" was wrong (Dave: CS 0x824, eip 0xF87C = 0824:763C).
4. **Segments and LE deltas change per boot** (launch method, `.conf`,
   `memsize`). Derive them each boot; notes record image addresses or CS
   offsets, never absolute run-time addresses.
5. **`E8 rel16` is 3 bytes**: target = (site + 3 + rel16) & 0xFFFF.
6. **One client per port** (GDB 2159, QMP 4444). A failed tool can leak a gdb
   client: `pkill -x gdb` (never `-f`), then re-attach. Ports not listening?
   `grep 0A /proc/net/tcp | grep -E ':086F|:115C'`.
7. **`gdb_command` answers one call behind**, and runs only the first line.
8. **Run vs halt.** QMP (screenshot, save/load state, restart) needs a
   running guest; memory reads need a halted one. Keys sent while halted are
   queued; screenshots of a halted guest are frozen. Interrupt → read →
   continue → save.
9. **QMP `send-key` releases too early** for games with their own INT 9
   handler: send real down/up events with a hold of 0.08–0.3 s
   (`tools/dosrun.py key`).
10. **`load_state`** only into a session where the game already runs in its
    video mode; afterwards the BIOS mode byte 0x449 may lie (write 0x13 back).
11. **A `.state` is a ZIP whose `Memory` member is linear RAM** after a
    136-byte header: read it offline (`tools/real/statedump.py`). The VGA
    framebuffer is not in it.
12. **Time in BIOS ticks** (0x46C, 54.9 ms), not wall time: `cycles=fixed`
    is not real time on a busy host, and `clock()` delays end on a tick.
13. **Ghidra drops Watcom register arguments** (`in_EAX`): read call sites in
    the disassembly.
14. **Notes are not proof, and a check that cannot fail proves nothing.**
    Re-check a claim against the code before building on it; put the bug back
    and watch the check fail.
15. **One agent per worktree and per harness.** Commit each deliverable with
    explicit paths as soon as it exists; never end with a dirty tree.
16. **What is on the screen may not come from the file you decoded.** Read
    the BIOS mode byte 0x449 first; then find the screen's bytes in RAM (see
    METHOD.md, "Finding assets from the screen"). [dave]
17. **The same call failed twice with the same message: stop repeating it.**
    Change the approach, or mark the item BLOCKED and work offline.

## This project's traps

(add entries here; tag generic ones `[generic]`)
- Watcom defaults to natural struct packing; the game's structs are pack(1) (see findings/executables.md).
  A port struct that is 2 bytes too big shows up as `imul $0x6a,%e..` where the game has `imul $0x68`.
  Fix with `#pragma pack(1)` around the struct. [generic]
- **Watcom 9.5b `-oe` inlines small user functions and nothing turns it off while keeping `e`'s other
  effects** (com_open's frame needs `e`; `-oae=N` N<=4 stops the inlining but also loses com_open and
  ~11 other functions; no `class=`, `not_inlining`, `off` aux directive exists; 9.5b has no `asm`/`_asm`
  keyword and `#pragma aux` strings never emit a body for a C-defined function). The game calls
  seekdat/telldat/datsize/readdat directly at 24+ sites; our build inlines them (11 bytes extra per
  readdat call). Write the C calls anyway; expect near-miss in fn_17928, fn_1832c, fn_357ac,
  f27060, f2a92c, waitkey until the game's trick is found. [generic]
- **Watcom 9.5b stack-slot assignment for locals is neither declaration order nor first-use order**;
  it is a decl+touch interaction (probes P15-P24: with 5 arrays + shorts, swapping the *touch* order of
  two arrays flipped their slots, but mirroring the game's touch order did NOT reproduce the game's
  slot map). Symptom: identical code with all `0x300/0x314/0x328/0x33c(%esp)` offsets permuted.
  Tool: dump both listings and compare the esp-offset multiset; iterate declaration order. Not solved
  for fn_1832c (pcx/card slots swapped => near miss). [generic]
- **`#pragma aux f(value [edx])` is E1121 (invalid return register) in 9.5b, but `value [dx]` is
  accepted and emits the full 32-bit EDX reads/ops** (`add $0x3e80,%edx`) - use it for asm routines
  that return in EDX (e.g. this game's f375cd at 0x375cd). [generic]
- **Call-site argument count must not contradict the definition**: Watcom 9.5b rejects 2-arg calls to a
  1-param function (E1151) even K&R-style. When the game passes extra args that the callee ignores
  (voice_play at 0x18da1 loads EDX *before* the call because the following showlines() call wants it
  there), give the *next* call's extra arg via the aux-pragma'd function's parameter list instead. [generic]

- **Watcom 9.5b promotes a global read inside a loop into a register, with a load at the loop
  entry and a store at its exit** (wait_or_key 0x14c7c: `mov edx,[0x7ebc0]` at entry,
  `mov [0x7ebc0],edx` before `ret`; fn_193a8 does it 10 times at loop entries and 5 at exits, ~90
  bytes of the gap there). The same source written the same way only gets the dance in some
  functions, so a size gap of 6-byte load/store pairs at loop boundaries is a compiler choice,
  not a missing statement. Do not add a local copy of the global to force it - that changes the
  body instead. [generic]
- **Watcom drops never-used locals from the frame**: adding an array to pad a frame to the game's
  `sub esp,N` does nothing (the slot disappears). A frame-size gap therefore means the game *used*
  those slots - look for stack writes whose displacement changed because of a `push` of a stack
  argument (see findings/menus.md Traps) before inventing locals. [generic]
- **optpin probes can mislead on flags**: `-oi/-ol/-or` were pinned from single-function probe files,
  but in the full build each of them breaks com_open's bytes while sendstr/find12/crchware match
  without them. Re-check every pinned letter against the whole-program match count. [generic]
- **Watcom 9.5b homes a boolean defined in a jump-target block in EBX and copies it to the merge
  register**: `cmpw $0,[cfg]; sete al; movzbl al,ebx; mov ebx,eax; test ebx,ebx` where the game has
  `sete al; movzbl al,eax; test eax,eax` (ctl_label 0x19d0e). The 2 extra bytes shift every later
  jump's displacement, so ~74 lines of the side-by-side listing differ from one instruction.
  Probed and unchanged: `x == 0`, `!x`, `?:`, `short`/`char`/`register` on the variable, a
  `radix` local, an unused third parameter, `!(X && Y)` vs `||` forms of the outer condition, and
  Watcom 9.5/9.5a. When a near miss starts at a `setcc`/`movzx`/`lea` pair, count the size gap
  first (`Code size:` from WCC.OUT) - one wrong register can account for the whole diff. [generic]
- **The prologue's `push ebx` is a register-allocation signal, not an ABI requirement**: 9.5b
  pushes EBX when a local *value* lives in it, but not when EBX is only loaded as a call argument
  (itoa's radix at 0x19d57); adding an unused third parameter to ctl_label made push/pop ebx
  vanish and the code 2 bytes shorter. So a game prologue that pushes a register ours does not
  means a variable lived there - look for a value the game keeps in that register. [generic]
- **`strcpy(p, "")` is a one-byte copy from the string pool** (`mov al,[0x600ac]; mov [p],al`), and
  pooling puts that `""` on another literal's NUL - 0x600ac is the terminator of "WACK"/"ack" at
  0x600a4. Before naming a "global char" that many functions read, check whether its address falls
  inside a literal. [generic]
- **A Watcom switch's jump table precedes the function and is not in body order**: 0x19da8 holds
  [0x19e02, 0x19e37, 0x19e6f, 0x19dde] for the cases of 0x19db8's `switch (v88bb2)`, i.e. the source
  wrote case 4 first and the bodies follow source order. Read those dwords before naming the cases -
  assuming body order == label order mapped every class off by one. [generic]
- **Which pseudo gets EAX is a coin flip we cannot pin**: 9.5b homes the *long-lived* local in EAX
  (never saved) and puts short-lived temps in a pushed register, the game the other way round (fn_19db8
  0x19dbb: game `mov dx,[0x88b84]` + `mov ax,[0x88bb2]`/`cmp ax,3`, ours the reverse; ~30 C shapes and
  every -o<letter> did not change it). When the two entry values of a function swap registers, treat it
  as understood, mark `differs:`, and move on - it is question 1/2, not a missing statement. The same
  tie-break picks which of two pushed-register locals gets ESI and which EDI: pcx_load 0x27060 homes
  the incoming index in ESI (`mov esi,edx` 0x2706a) and the byte count in EDI, ours the other way
  round, and 24 spellings - both locals' types, separate/last/split declarations, `register`, the
  index copied into a fresh local, a pointer temp at the definition and at the use, struct-pointer
  and index temps, a two-statement count, the `sub` moved to the use, `SEEK_SET` written 0, and the
  *global externs declared in the other order* - leave the five rows untouched (.work/probe_pcxload,
  iter 18). Global declaration order is not a lever either; stop after one probe compile. [generic]
- **The coin flip also decides whether a lone local's register is saved, and 9.5b cannot be told
  otherwise** (netkeys_out 0x2d9e4, iter 23): with exactly one byte accumulator and no other pseudo,
  9.5b homes it in EAX and emits no prologue (`xor al,al`), the game homes it in EDX and saves it
  (`push edx` ... `pop edx`, 0x2d9e4/0x2da20); netkeys_pack 0x2da28 and netkeys_in 0x2d94c (c in DL,
  AL as the mask temp) are the same flip. Nothing moves it: local type (char/signed char/short/int),
  `register`, an unused second local (int/char, both declaration orders), an unused parameter, a
  non-void return type with no `return` (W107), `return c`, an `unsigned char ca[1]`, `|=` vs `+=`,
  `c = c + N`, store order - 21 spellings score 10 in .work/probe_nko/.work/probe_nk2, and WC95,
  WC95A and WC95B compile the control shape to the same 60 bytes (masked function bytes equal in all
  three OBJs). And 9.5b has **no explicit register variables**:
  `register unsigned char c @ edx;` is E1009 ("Expecting ';' but found '@'"), so a local's register
  cannot be pinned by the declaration at all. Treat the whole `c in DL` net family as this flip. [generic]
- **Literals land at the game's CONST addresses as long as functions are added in address order**: our
  "AMATEUR. "/"PRO. "/"CHAMPION. " got offsets 0x948/0x954/0x95c = 0x6094c/0x60958/0x60960 with the
  base check.sh prints (`segment CONST base 0x060004`). If that line splits into two bases, a literal
  is out of place - fix the source order before hand-patching addresses. [generic]
- **Watcom slides global stores across an inline `strcpy`'s string-op unit** (`mov ecx/mov edi/
  mov esi` + `rep movsd` + `movsb`), and which of a group slide is not obvious from the C: in
  classes_menu the three stores before the fourth row's strcpy land as the game has them
  (`v88bb0 = 6` stays above the setup movs at 0x19f01, `v88b9e = 0` at 0x19f19 and the `nrows = 4`
  def at 0x19f22 move inside the unit, before the `rep` at 0x19f27), while writing the same three
  statements *after* that strcpy puts the first inside the unit and the rest after the copy. So a
  store that sits between the setup movs and the `rep movsd` is a source-position fact to check,
  not a mystery: generate one probe file with the statement at every position around the copy and
  compare (`tools/wc/variants.py`, one compile for 40 variants, ~4 s). [generic]
- **A chained assignment computes through a scratch; the two statements load in the local's home**
  (demo_play 0x2e348, iter 18): `k = v88cca = run[i].k` loads the byte into AL, stores it to v88cca
  and copies AL to k's home BL, and the store takes AL's short `a2 disp32` form; `k = run[i].k;
  v88cca = k;` loads `mov bl,byte [eax+0x7d9a2]` and stores `mov byte [0x88cca],bl` (6 bytes) - the
  game's shape, and it made the function the game's 172 bytes (the byte count is part of the match).
  Pair it with the next entry. [generic]
- **9.5b defers a store to a global; `volatile` pins it in place** (demo_play 0x2e35f, iter 18):
  with `k = ...; v88cca = k;` the store slid ~80 bytes down to just before the last store of the
  function, wherever the statement was written (all nine positions between the masks give the same
  place); declaring the global `volatile` keeps it where the statement is. Declare the volatile type
  under a second name when the plain name is still wanted elsewhere - `extern volatile unsigned char
  v88ccav; #pragma aux v88ccav "_v88cca"` fixes up to the same symbol. It does **not** move an
  adjacent register copy: 9.5b still emits store,copy,and where the game has copy,store,and, and 46
  spellings (a second local carrying the first mask, comma forms putting the store inside the AND, a
  pointer store) left that one slot. [generic]
- **But not every store placement is reachable**: with `v88bb2 = 1; v7edb0 = 1;` 9.5b hoists the
  first of the pair into the eighth help line's unit (0x1a054, like the game) and sinks the second
  to just before the following call (0x1a080), where the game has it at 0x1a05d; swapping the two
  statements swaps which one moves. 72 placements of the pair (before each of the last six help
  lines and after all nine, both orders), a folded-away `if` around the second store, and
  `v7edb0 = v88bb2` to make it depend on the first all gave the same single instruction out of
  place (149 instructions, 667 bytes) - accept it as a near miss with `differs:` and move on
  (QUESTIONS.md 3). Counter-case, laps_menu 0x1A148: `v88bb0 = 0xd; v88c80 = 1;` lands in the third
  copy's unit from all three source placements (before the first copy, between the second and third,
  after the third - same bytes, `.work/probe11`), so that pair slides all the way and its placement
  is not a lever there. [generic]
- **The register that homes a local follows the position of its first definition, not its
  declaration**: classes_menu's `short nrows` defined before the fourth row's strcpy got EDX (the
  game's home, 0x19f22); defined at its later use it got ECX, and `menu(nrows,0,0,0)`'s argument
  setup reordered with it. Try moving the assignment before the preceding copy before re-probing
  types, `register` or -o letters (same family as question 2). [generic]
- **A shared load lands in the register of the pointer local defined from it FIRST, and a doubled
  scalar written at the use site becomes a `lea` where a local holding the double gives
  `add reg,reg`** (track_pics_load 0x277b8, iter 20): the game loads v7d75c into ECX, spills q, then
  `add eax,eax`, `mov ebx,ecx`, `add ecx,eax` (0x277db-0x277e9); `a = v7d75c; b = v7d75c + q * 2;`
  loads into EBX and doubles with `lea ecx,[eax+eax]` because q stays live. Two temps fix both:
  `d = q + q; c = v7d75c; a = c; b = c + d;` with **`c` declared before `a` and `b`** reproduces all
  five instructions (0 instruction diffs, `.work/tp2/TP.C` tp_d; the same body with `c` declared last
  is 7 diffs, tp_c). Local declaration order does pick the register here even though it does not pick
  the stack slots (two entries above). [generic]
- **Write the next statement's load before the assignment whose halves the game splits**: `-oe`
  hoists a global load above the shift/store of the statement written before it and sinks that
  statement's store (fn_16ca0 0x16cf9, iter 22). The game runs `movsx eax,[v88c7c]`,
  `mov [v88c54],0x78`, `sar eax,1`, `mov dx,[v7ea58s]`, `mov [v88c7a],ax`, `cmp dx,2`
  (0x16cf9-0x16d18); the source order `v88c54 = 0x78; v88c7a = v88c7c >> 1; i = v7ea58s;` came out
  `movsx, sar, store7a, load i in AX, store78` (the store slid down by itself) and `i` sat in AX.
  Writing `v88c54 = 0x78; i = v7ea58s; v88c7a = v88c7c >> 1;` reproduced all six instructions and
  put `i` in DX: 10 rows -> MATCH. Reading a listing where a load sits between the two halves of an
  expression, move that load up in the source before scheduling by hand. [generic]
- **A `mov [esp],eax` immediately followed by `mov edx,[esp]` is an allocator spill, not source**
  (view_at_curpos 0x2f408, iter 20): the game loads the first operand of `v7d7a4 = v7d610 + v88b0a`
  straight into EDX, ours routes it through the slot it uses later for the second sum's first
  operand. 30 spellings - both operand orders in both sums, statement order, casts, a temp for the
  sum, temps for the operands, an `int y = v88b0e` local - never drop it, and the frame rules out
  the local route: any local adds a SECOND slot (`sub esp,8`) where the game has one. Check the
  prologue first: if our frame already equals the game's, the function has no locals to add. [generic]
- **A `volatile` local is how you make 9.5b emit the frame slot and the spill/reload pair the game
  has** (shot_step 0x25e6c, iter 23): the game computes `t = v7eb9c * 8` through a 4-byte slot (`sub
  esp,4`, `mov [esp],eax; mov ebx,[esp]`, 0x25f43-0x25f46). A plain `int t` - and long, an array, the
  other declaration orders, or no temp at all - keeps it in a register and never grows the frame: 11
  rows. `volatile int t` emits the slot, the store and the reload: 11 rows -> 2, and the 2 left are
  the *order* of the two loads (the game reloads t into EBX at 0x25f46 before loading the base into
  EAX at 0x25f49, ours the other way); `p = t; ... (int *)(p + v7d6a0);` gets that order but p takes
  EAX and the base EBX (3 rows, the coin flip). Reach for it when the game's prologue has `sub esp,N`
  and a value round-trips through [esp] - the frame-vs-register question of the entry above, from the
  other side. [generic]
- **An EAX/EDX swap in an address computation yields to an EAX-homed local** (songs_load 0x3360c,
  iter 14): `makepath_x(path, names[i])` built the offset in EDX, the game in EAX; seven index
  spellings and a fresh temp did not change it, `n = i * 12; makepath_x(path, (char *)names + n);`
  matched (`n` is the local that later takes datsize()'s EAX result). Try this before writing an
  EAX/EDX swap off as the coin flip above. [generic]
- **A cold `then` block is emitted after the rest of the chain**: the game's `test eax,eax;
  jne <far block>` with the block after the else-chain (classes_menu 0x19f61 -> 0x1a107) comes from
  `if (!two) { ...big... } else v88bb2 = 4;`; writing the obvious `if (two) v88bb2 = 4; else if
  ...` makes 9.5b put the block inline and add a forward `jmp` (+1 byte, different jump
  displacements everywhere after it). [generic]
- **A `switch` on a `short` with three cases is a balanced compare tree, not a jump table**: cases
  1,2,3 give `mov ax,[global]` once, then `cmp ax,2 / jb <below> / jbe <case 2> / cmp ax,3 /
  je <case 3>` with the three store blocks emitted as cold blocks after the chain (laps_menu
  0x1A148, 0x1a21e and 0x1a2b8). Writing it as `if (nlaps == 1) ... else if ...` costs 30+ bytes:
  every test re-loads the global (58 instruction diffs against 1 for the switch shape). Probe the
  tree before anything else. [generic]
- **Whether two identical store blocks merge depends on the C shape, not the constants**: the game
  jumps from the TIME TRIAL test straight into the switch's `nlaps = 6` block (`je 0x1a246`,
  0x1a1e6); `if (v88bb2 == 4) { nlaps = 6; return; }` before a `switch` emits the store twice, while
  `if (v88bb2 != 4) { menu ... } else nlaps = 6;` shares it and matches byte for byte. Probe the
  orientation of such a test with `tools/wc/variants.py` (30 shapes in one compile here) rather than
  assuming the copy is a bug. [generic]
- **Watcom puts switch jump tables in the gap *before* the function, so a gap is not dead space**:
  0x1a360-0x1a37f (recorded as "padding" in a STATUS note) is two 4-entry tables used by
  `jmp dword ptr cs:[esi*4+0x1a360]` / `jmp dword ptr cs:[edx*4+0x1a370]` inside `cars_menu`.
  Disassemble a gap before calling it padding; the `jmp dword ptr cs:[reg*4+disp]` + range check
  (`cmp reg16,N / ja default`) is the tell, and the table order is source-label order (iter 3).
  [generic]
- **A call target can be an interior label of a library module**: 0x430f7 is 0x39 bytes into the
  clib `__stos` module at 0x430be (165 bytes, `libmatch.py -l ww .../DOS/CLIB3R.LIB`); declare it as
  its own extern and pin the argument registers with what the call site loads (`#pragma aux
  fn_430f7 parm [eax] [edx] [ecx]`, ebx untouched) - the fixup check then verifies it by name like
  any other clib symbol. [generic]
- **Watcom does not allocate scalar stack slots in declaration order** (386-byte probe plus
  `cars_menu`, iter 7): arrays do (three `char[0x50]`s and two `short[8]`s land in declaration
  order, ascending from `esp+0`, and reversing the declarations permutes them), but the scalars
  get their `esp` offsets from the register allocator's order, not the source's: a 1250-instruction
  function whose every statement was translated in the game's order still came out with the same
  26 spilled locals in a different permutation, and one local the original spills stayed in a
  register here (frame 4 bytes short). Expect a whole-function slot permutation as a near-miss
  class; compare the two listings with the `disp(%esp)` text masked before hunting for a
  translation error. [generic]
- **Read esp offsets per instruction, not per table**: Watcom's 5-argument calls push one dword,
  so every displacement after a `push 0` is 4 below the value that a slot table sorted by printed
  offset implies (`cars_menu` 0x1adf7/0x1ba75 read 0x138/0x15c, not 0x13c/0x160). [generic]
- **Defining a small game function in `src/ww.c` lets `-oe` inline it into every caller and can
  cost a function that already matches** (iter 8): `fn_1c068`/`fn_1c0ac` (68/69 bytes) rebuild
  byte-identical but inlining them into `net_end` 0x110d4 broke that match. No per-function off
  switch: `-oe=4/8/16/24/32/48`, a bare `#pragma aux name modify [...]`, and a `volatile`
  function pointer all fail (only N<=8 stops inlining, and that build matches 73-76 not 84).
  Check the callers first (`tools/le/xref.py ww 0xADDR`); if a byte-identical function calls it,
  keep it `extern` and write its C into the findings file instead. [generic]
- **`tools/wc/wcc.sh` runs WCC386 inside DOSBox: a file name longer than 8.3 fails silently**
  (`Unable to open 'SETUP_PROBE.C'`, 0 lines, no OBJ). Keep probe names <= 8 characters. [generic]
- **A candidate function start is not a function - but no-caller is not proof either** (iter 9,
  corrected in iter 10, [generic]): `tools/wc/starts.py` prints `after-ret` guesses, and
  `grep "call 0xADDR"` finds nothing for a jump target (0x1c66c is the target of the `jbe` at
  0x1c660). But 0x1c88c and 0x1c968 in the same range ARE functions - own `push` prologue, own
  `ret`, own 4-byte padding - that nothing in the image references (no E8/E9 rel32 to them in the
  code object, no absolute dword in either object, `xref.py` agrees): Watcom kept dead code from
  the module. Decide with the prologue/`ret`/padding, not with the caller count; measure the
  previous function's extent from its own `ret`, not from the next *named* address.
- **`MATCH (fixup bytes not compared)` can hide a wrong global order** (iter 10, [generic]):
  objmatch masks the 4 bytes fixups fill, so a store sequence that assigns the same values in a
  different order still says MATCH. Always read the `WRONG sym@addr:` lines after it - they showed
  `fn_1c88c`'s tail loading `save_c` where the game loads `save_a`. Watcom's store order follows
  the source statement order loosely: `pal_b; pal_c; pal_a` reproduced the game's bx/ax pairing
  where the semantically equal `pal_b; pal_a; pal_c` did not (probe file `.work/sh2/S.C`). [generic]
- **Diff a near miss with `tools/wc/fndiff.py`, not with two listings** (iter 10, [generic]):
  `tools/wc/objdiff.py` aligns bytes, so one 1-byte drift marks the rest of a 1900-byte function;
  fndiff aligns instructions and prints the differing pairs (it found 5 real shape differences
  inside 119 marked lines).
- **`fndiff.py` prints OURS on the left and the game on the right** (iter 20, [generic]): the
  docstring never says so, and a `differs:` note written from its output can swap the two sides
  (track_pics_load's note described the game's shape as ours since iter 14). `A` is the .OBJ,
  `B` the game bytes; confirm with a full disassembly of one side before writing prose about a diff.
  The nightly prompt's own parenthetical says the reverse - the `fndiff.py` docstring now states the
  order (iter 21, after a `release_check` note was written backwards once more).
- **A local's type decides whether Watcom keeps it in a register** (iter 10, [generic]): the
  centring temporary of `fn_1c0f4` as `int` got a stack slot at esp+0x3d4 (frame 0x3d8, every
  displacement off by 4); as `short x` it stayed in EAX, the frame became the game's 0x3d4 and the
  int-to-short argument became the game's `cwde`. The reverse also happens: `scores_screen`'s loop
  counter `k` as `short` took a stack slot and grew the frame to 0x5c (the game's 0x58), pushing
  every scalar displacement up by 4, while `int k` left it in ECX exactly where the game has it
  (0x1cf8c) - 113 diffs became 97. Try the type before restructuring. [generic]
- **A gap between two arrays in the game's frame is an array it sized larger than its code uses**
  (iter 12, [generic]): `scores_screen`'s frame is 0x58 with the name buffer at esp+0x28 and the
  stamp at esp+0x3c, but the only writes below 0x28 are four dwords (16 bytes). Declaring that
  array `long sc[4]` put the two buffers 0x18 too low; `long sc[10]` (40 bytes, 0x00-0x27, still
  only indexed 0..3, 0x1cebc-0x1cf57) lands all three arrays on the game's slots. Size a local
  array from the frame, not from the largest index the code shows. [generic]
- **A conditional store to a global: the game writes the two branches, Watcom turns a
  ternary into a value select** (iter 11, [generic]): `pal_a = pal_a ? 0 : 0x3e80;` compiles
  to `cmp / je / xor eax,eax / jmp / mov eax,0x3e80 / mov [pal_a],ax`. The game's shape at all
  four `cars_menu` flips (0x1a74e, 0x1a9d8, 0x1aeb9, 0x1bb16) and at both `enter_name` flips
  (0x1cbf8, 0x1cd6a) is `cmp word [pal_a],0 / jne / mov word [pal_a],0x3e80 / jmp / mov word
  [pal_a],0`. Scan of the whole code object: 55 `mov word [mem],0x3e80` sites (regex
  `\x66\xc7\x05....\x80\x3e`) and ZERO `mov eax,0x3e80` (`\xb8\x80\x3e\x00\x00`) - the game
  never materialised that constant in a register, so it never wrote a ternary of this shape.
  `if (pal_a == 0) pal_a = 0x3e80; else pal_a = 0;` reproduces it; writing the four `cars_menu`
  ones that way took `cars_menu` from 749 to 678 aligned instruction diffs with no other change
  (and 6564 -> 6584 bytes against the game's 6861). [generic]
- **Declaration order moves the local slots but does not pick their permutation** (iter 11,
  [generic]): for `enter_name` 0x1cb44 the frame size (0x74), the buffer/`union REGS`/`struct
  SREGS` placement and even which parameter spills to which slot (`n` at esp+0x50, `heading`
  at 0x54) all follow from the local declaration order - but 43 probed declaration orders (three probe
  files, GUIDE "probe recipe", scored with `tools/wc/variants.py ... 0x1cb44 0x1ce13 <names>`) never
  put the seven locals in the game's permutation (xc, cy, x, nx, lim, y, cx from 0x58 up); the best is 21 diffs, all of
  them displacements. The types mattered more than the order: `int ch` cost 70 diffs, `char ch`
  and all-short locals cost 41 (GOTCHAS iter 10, "a local's type decides whether Watcom keeps
  it in a register"). Ask "which variable lives in which register/slot" before "in what order
  are they declared". [generic]
- **`-oe` tail-merges an identical statement that ends both arms of an if/else, and which arm keeps
  its copy follows the register the constant lands in** (iter 13, [generic]): in `race_records_menu`
  0x1d13c both arms of `if (v7ecb4 == 0) {strcpy(row,"") x4} else {" BONUS" x3 + strcpy(row,"")}`
  end with the same `strcpy(&mnames[0xb4], "")`. Ours loaded the pooled "" byte 0x600ac into EAX,
  which let the optimizer serve both arms from one store after the join (504 bytes, and the arm lost
  a store); the game loaded it into EDX, so the two arms each keep their copy (its 0x1d178-0x1d196).
  The register also decides the size: `mov al,[abs]`/`mov [abs],al` are the AL-only 5-byte forms
  (`a0`/`a2`), EDX needs the 6-byte `8a 15`/`88 15` - so a near miss that is one load+N stores
  shorter, with AL where the game has DL, is this merge and not a wrong expression. Flipping the
  condition to `if (v7ecb4 != 0)` un-merges but turns the game's `jne` into `je`: two changes, not one.
  Moving one arm's copy to the *head* of that arm un-merges without touching the branch (504 -> 512
  bytes, 13 -> 12 rows) but leaves the store at the head; the register itself is out of reach - 11
  spellings never got EDX (`char c = *"";` folds to `mov byte [abs],0`, `"\0"` copies 2 bytes). [generic]
- **`-oe` sinks a zero argument constant into the following test but leaves a non-zero one alone**
  (iter 15, [generic]): the four inlined `music_start(song, 0)` copies in WW.EXE (found by scanning
  the code object for `66 c7 05 cc8b08 0000`, the `mov word [0x88bcc],0` of the body) all keep
  `xor edx,edx` before the
  inlined stores (0x11885 music_jingle, 0x2b6a3 duck_screen, 0x2cad6 race_results, 0x2e729 trial_book),
  ours puts it after the `cursong` store - one moved instruction, 2 diffs, in all four functions.
  The inlined `music_start(song, 1)`'s `mov edx,1` is early in both builds, so it is the *zero* that
  moves. Tried and identical (116 bytes each, `tools/wc/variants.py` probe with the real music_play /
  music_start bodies): `int lp = 0;` then `music_start(song, lp)`, `short lp`, `register int lp`, the
  same local passed to both calls, an unused second parameter, spelling the inline out by hand,
  swapping music_start's two stores, `#pragma aux music_start parm [eax] [edx]`. Cost 0 more variants
  until someone finds what makes 9.5b define a zero early. The same "the game defines early, our build
  sinks/hoists it" shape sits in track_setup 0x124e8 (the game has `mov ebx,[trk7ebc8]` at +8 and
  `mov eax,[track]` at +14, ours swaps those two 6-byte loads): assignment order, `int y, x;`
  declaration order (that changes the register choice, 13 diffs), `register`, `unsigned` and switching
  on a local copy all give the same bytes. `-3` instead of `-3r` is not the cause either - the whole
  object comes out byte-identical (182 MATCH either way), so do not re-litigate the reorder flag for a
  moved load. The same dead-copy shape at a function entry: fn_182d0 0x182d0 opens with
  `mov esi,ebx` (a copy of the EBX parameter) right before its own `imul esi,ebx,3`, and 12 spellings
  over two sessions (`t = a3`, comma, `register`, the expression in both loops, `lim = a3;` then
  `lim *= 3;`, `lim = lim * 3`, `lim = (short)(a3*3)`, a `long n = a3` read by `if (n == a3)`, a
  `long t = a3` as both loop bounds, an `int` counter, two limit locals) all compile the copy-free
  88 bytes (iter 16). Treat "the game has one dead copy of an incoming register" as LAYOUT-ish.
  The same unreachable tie-break for the *home register of a loop-carried local*: net_recv 0x2db00's
  `crc` (game ECX: `movzx eax,cx` at the loop top, `mov ecx,eax` at its end; ours EAX with
  `mov edx,eax`) stays put through 21 spellings - zero-store order, chained zeroing, a short/int temp
  per call, loop forms, argument casts, declaration order, init order (iter 17). `int crc` does move
  the home to ECX but adds the zero-extensions the game does not need, 4 rows -> 5.
- **`tools/wc/fndiff.py`'s diff count is not the number of real differences** (iter 13, [generic]):
  it aligns only `min(len(ours), len(game))` instructions, so when ours is shorter its whole epilogue
  shows up as ours-only rows (`race_records_menu`: it printed 13 pairs where the real difference was
  5 in one block, 8 were its own cut-off `add esp,4`/`pop`/`ret`). Read the blocks and the
  instruction counts (`tools/wc/fncompare.py` prints both, plus pairs whose text agrees but whose
  byte length differs, e.g. rel8 vs rel32) before concluding anything from the number.
- **A `tools/wc/variants.py` score counts the variant's own alignment filler, so it is one higher
  than `fndiff.py`'s** (iter 15, [generic]): with `0x11884 0x118f6` music_jingle's control variant
  scored 3 where `fndiff.py` reports 2 - the game range stops at the public after it, before the
  game's `mov eax,eax` filler, while our 116-byte copy carries one and the variant that happened to
  compile 114 bytes scored 2 for byte-identical instructions. Compare variants with each other at
  equal byte size, never a variant score against a `fndiff.py` count, or a spelling that changed
  nothing reads like a win by one.
- **A `variants.py` probe ranking does transfer when the probe *defines* the game function the body
  inlines** (iter 22, refines the iter-21 trap): the `com_expect` probe (`CE.C` with `com_getc`
  defined above it and the same externs) came out 160 bytes with its control scoring 9, exactly
  `fndiff.py`'s count for the whole-file function, so its 10 spellings ranked truthfully. A probe
  that only *prototypes* the inlined function is a different, smaller function (iter 21).
- **A `tools/wc/variants.py` probe file must `#include <string.h>` (and the same prototypes) or it
  compiles a different function** (iter 13, [generic]): my probe of `race_records_menu` (six spellings,
  no includes) gave 332-byte variants against the 514 bytes the same body produces inside `src/ww.c`,
  because Watcom then emits real `strcat`/`strlen` calls instead of its inlines - so the probe's
  scores and register choices were not the whole-file ones. Copy the includes/extern block over
  verbatim, and confirm the probe's size is within a few bytes of the real one before believing a
  ranking. Same trap when the function inlines a *game* function that `src/ww.c` defines earlier:
  a `com_wait_byte` probe with only a `com_getc` prototype came out 80 bytes against the file's 152
  and its control variant scored 24 diffs where `fndiff.py` reports 9 (iter 21). For whole-file
  answers, edit `src/ww.c` and diff with `tools/wc/fncompare.py` (aligned
  pairs, instruction counts per side, and pairs whose text agrees but whose length differs).
- **`mov r, N*stride` + `add r, sym` in a loop names the array's real base** (iter 14, [generic]):
  trial_records_menu 0x1d579/0x1d584 is `mov ebp,0x3c; add ebp,0x7bc00`, which is i*0x3c + base with
  i starting at 1, so the base is 0x7bc00 (`mrows`), one row before `mnames`. `&mnames[(i-1)*0x3c]`
  folds to `mov ebp,mnames` (38 diffs) and `mnames - 0x3c + i*0x3c` gives 10. Declaring
  `extern char mrows[][0x3c]` at 0x7bc00 and writing `&mrows[i][0]` was byte-identical. When the
  `add` immediate is not a known symbol, name a new array there before rewriting the index.
- **A `short` parameter whose callers do not sign-extend means the callers had no prototype**
  (iter 14, [generic]): fn_1d704 0x1d704 compares its first argument as a word (`movsx eax,bx`,
  `cmp word`), but `void fn_1d704(short, int);` made both callers add a `movsx` (race_records_menu
  504 -> 472 bytes). Declaring it `void fn_1d704();` (no parameter list) and defining it with
  `short which` keeps both callers and gives the callee its word compares.
- **A retry loop whose success path returns: write `while (1) { if (x != 0) {...return} if (y != 0)
  break; }`** (iter 14, [generic]): answer_call 0x1e0f4 has the failed wait's test and the cancel
  block after the success path's `ret`. `while (!x) { if (y) {cancel} }` emits the cancel inline (54
  diffs); `for (;;)`/`do ... while (!y)` with the same body leave one instruction off; `while (1)`
  with explicit `!= 0` tests is byte-identical.
- **`tools/wc/variants.py` copies of one function share their identical tail** (iter 14, [generic]):
  9.5b merges the same epilogue across functions in one module, so every copy after the first jumps
  into the first one's tail (`jg` rel32 out of the function) and scores 50 bytes short with a wrong
  diff count. Score up to the start of the common tail (demo_mode: 0x1e69f, not the `ret`), or make
  the tails differ.
- **A stack slot the loop increments but never reads is a parameter used as the counter** (iter 14,
  [generic]): credits_page 0x1eee4 stores its 2nd argument at esp+0 and does `inc dword [esp]` each
  pass with no load; a separate `i = first` drops the slot (frame 4 not 8). Writing `first++` on the
  parameter itself keeps it - 9.5b does not delete stores to a parameter's home.
- **`movsx r,word [mem]` + `cmp r,N` is a short read into an int local; `cmp word [mem],N` is a direct
  compare** (iter 14, [generic]): ents_flags 0x213f0 tests `sprattr[id].kind >= 2` as
  `movsx eax,...; cmp eax,2`. Writing the compare on the field gives `cmp word ptr [eax],2` and costs a
  register, which pushed a local onto the stack; `k = ...kind; if (k >= 2)` matched. Also: a pointer
  stepped in the `for` header (`v7d7f4++, i++`) puts the store before the step, unlike `p++` in the body.
- **A `ret` is not a function's end; check that the next address has a caller** (iter 14, [generic]):
  kart_hit 0x216a0 returns at 0x2175e from one branch and continues at 0x2175f, which `starts.py`
  lists as "after-ret". Rebuilt to that ret it looked like a register-choice near miss (EBX/ECX pushed
  but unused, 11 forms tried); the rest of the body used them. Before calling a near miss, run
  `xref.py` on the "after-ret" address: no caller, but a jump from the function above, means it is
  still the same function.
- **Repeated `store; pop...; ret` tails are one shared `store; return;` the compiler duplicated**
  (iter 14, [generic]): shot_hit 0x218a4 has `mov word [v88bae],3` + epilogue six times. Writing
  `v88bae = 3; return;` in each branch lets 9.5b schedule the store above the branch's last store;
  letting every branch fall into one common `v88bae = 3; return;` keeps it last (38 -> 33 scored).
- **`movsx eax,r; sar eax,1; mov edx,eax` (computed in EAX, then moved) is a store through a temp
  variable** (iter 14, [generic]): kart_line 0x21de0 matched only with `t = dx >> 1; err = t;` (the
  swap temp reused); `err = dx >> 1` computes straight into EDX. Its other levers: every local
  `short` (sx/sy too: the game's `mov esi,-1` is still a short), the declaration order that gives the
  game's registers, and `dy < dx`. line_points 0x21554 has int sx/sy (dword stack slots) and does
  not take these levers. Not a rule: in line_draw 0x21f04 (the same walk, plotting) the direct form
  is the one that goes through EAX, and the game's `movsx edx; sar edx,1` was not reproduced.
- **9.5b unrolls a constant-count loop itself** (iter 14, [generic]): crack_draw 0x22034 does pass 0,
  then a loop of three passes per iteration over a byte offset (`add edx,0x30; cmp edx,0x70`). A plain
  `for (i = 0; i < 7; i++)` matched; do not write the unrolling into the source.
- **When the game CALLS a small function that -oe inlines in ours, call it through an aux alias**
  (iter 14, [generic], QUESTIONS 6): `short com_getc_x(void); #pragma aux com_getc_x "com_getc_";`
  then `com_getc_x()` at the sites the game calls. The inliner never sees the alias's body, the
  fixup names the real symbol, so the fixup check passes. For a function the game calls everywhere
  (readdat), `#define readdat readdat_x` right after its definition. Fixed dial, link_open, modem
  and kart_state's kart_anim2 calls.
- **Switch case bodies are laid out in source order; the jump table tells you that order** (iter 14,
  [generic]): main_menu 0x1f858 has case 7 at 0x20df5 before case 6 at 0x210ba. Writing case 6
  first made fncompare misalign the whole tail (596 vs 360 instrdiffs). Sort the case labels by
  their jump-table target address before writing the switch.

- **The seekdat/readdat call-site aliases are not universal**: effects_load 0x2754c inlines the real
  seekdat/readdat (fseek/fread directly, its argument loads in the inlined order); writing fseek/fread
  by hand loads datfile before the buffer. `#undef seekdat` / `#undef readdat` around such a function
  (and re-#define after) gives the game's code. Check which form a site has before writing it.

- **Pointer chains scheduled when typed char ***: data_load 0x28180 carves a block with
  `p1 = (p0 = base) + size0` ... The game's code is a plain `mov/add eax,[mem]` chain; with char *
  globals our build hoists the size loads into ECX/EDX (+8 insns). The same expression on int
  aliases of the globals (`extern int a; #pragma aux a "_v7d6a0"`) matched byte for byte.
- **A re-test the game makes and we skip means the stores between were volatile** (iter 14,
  [generic]): race_results 0x2bff0 does `if (netgame != 1) { clear key bytes }` then
  `if (netgame == 1)` and the game emits a second `cmp`; our 9.5b reuses the flags across plain
  byte stores. Writing the key bytes through `volatile` aliases (`extern volatile char vk;
  #pragma aux vk "_key88cd5"`) restores the cmp. Making the global itself volatile lost a match
  elsewhere, so alias per function.
- **Two-digit padding stores: `buf[1] = buf[0]` before `buf[2] = 0`** (iter 14): the game's
  itoa-then-pad idiom loads buf[0] first (bigdigits 0x2b558 MATCH, also in leaderboard and
  overlay_draw); not at every site (overlay_draw's third pad keeps the other order).
- **fncompare "0 diff blocks" is not a MATCH** (iter 14, [generic]): it shows displacements that
  differ as `@`, so `mov [esp+0x10],bl` vs `mov [esp+0x14],bl` compares equal. net_send 0x2dc38 had
  0 diff blocks while check.sh said "differs at +13" (two locals' stack slots swapped; fixed by
  declaration order). Always confirm with check.sh before writing "byte-identical".
- **A byte-loop strcpy from a constant address is still a literal** (iter 14, [generic]): Watcom
  inlines `strcpy(buf, "TURBO")` as movsd/movsw/movsb; the game's songs_pick 0x32e60 has the generic
  `mov al,[esi]` loop with `esi = 0x61b34`. Extern char arrays matched the code but took the names
  out of CONST, so check.sh's `segment CONST` line went from "agree" to DISAGREES 0x60 bytes later
  (a later function's strings). `strcpy(buf, &"TURBO"[0])` gives the loop and keeps the literal in
  CONST. Watch the `segment CONST` line after every new string, not only the function's MATCH.
- **A `for` tail with two increments is rescheduled; the game's order comes from a guard clause with
  the pointer step at the body's end** (iter 16, [generic]): anim_tick 0x261fc ends its ents loop with
  `add ebx,0xc; inc ecx; cmp cx,[nents]; jl`, ours with the two increments swapped. Every spelling of
  the `for` tail failed (`p++, i++`, `i++, p++`, `++p, ++i`, `i++` in the body, `while`, `do`, a cast
  step `v7d7f4 = (struct ent *)((char *)v7d7f4 + 0xc)`) - 9.5b sorts the pair by its own dependency
  order, source order is ignored. What matched (3 diffs -> 1, and that 1 is the masked-fixup artifact
  below): the inverted test as a guard clause with the step after the body,
  `for (i = 0; i < nents; i++) { if (p->f2 == -1) { p++; continue; } ...stores...; p++; }`, so the step
  sits at the branch target (the game's `je 0x26247` -> `add ebx,0xc`) and only the counter stays in
  the tail. The same shape is already used in ents_flags 0x213f0 for other reasons.
- **A masked fixup disp prints as a fake `[reg - 0x6f6f6f70]` pair in fndiff.py** (iter 16, [generic]):
  masked bytes become 0x90, so a 4-byte displacement fixup reads back as a negative number and
  fndiff shows a `[edx - @]` vs `[edx + @]` row on a function objmatch calls MATCH. Treat that row as
  noise; the MATCH line is the verdict (iter 22: 7 of `self_project`'s 10 `src/nearmiss.txt` rows
  and 1 of `com_expect`'s 9 are that - subtract them before spending variants, and a `variants.py`
  probe carries the same fake row in every variant). `tools/wc/variants.py` also counted our *whole* function
  against the `[ADDR,END)` slice of the game (only the game side was cut), so its scores were
  dominated by the unaligned tail; both sides are cut now (iter 16) - re-run old probes before
  trusting a saved ranking.
- **A variant that scores well may be a different program: read which value the instruction reads**
  (iter 16, [generic]): line_draw 0x21f04 wants `movsx edx,cx` before its `mov eax,ebx; mov ebx,ecx;
  mov ecx,eax` dx/dy rotation. Writing the rotation first and `err = dy >> 1` after it reproduces the
  game's instruction text at the game's addresses - and multiplies the wrong half, because ECX now
  holds the old dx. Score a candidate only after checking, register by register, that the def reads
  the value the game's def reads. Same probe, same shape, wrong program.
- **A local's shift form (through EAX or into its own register) depends on code far away**
  (iter 16, [generic]): with `err` homed in EDX, `err = dx; err >>= 1;` compiles `mov edx,ebx; sar
  dx,1` while `err = dx >> 1;` compiles `movsx eax,bx; sar eax,1; mov edx,eax`; in a 10-variant probe
  the *same* source line took EAX or EDX depending on how the OTHER branch was spelled (the probe's
  EDX form did not survive the whole file: 4 diff rows became 13). Probes rank spellings for one
  function in isolation; before keeping one, compile the whole file and diff (GOTCHAS iter 13, "the
  probe's scores are not the whole-file ones").
- **A pointer the game re-materialises twice needs a second local, re-armed at the body's end**
  (iter 17, [generic]): fn_1c994 0x1c994's stamp loop (0x1ca83-0x1cac1) opens at `push edi` with no
  load (EDI is live-in), then `pop edi` + `mov edi,ebp` before the strlen and `mov edi,ebp` again on
  the back edge. Written with one local (`strcat(s, src)` / `strlen(s)`) 9.5b folds that pair and
  hoists a single `mov edi,ebp` to the loop head (5 rows); `char *p; p = s;` before the loop,
  `strcat(p, src)` inside it and `p = s;` at the body end after `src = " ";` puts the three loads
  exactly where the game has them (316 bytes, MATCH). The strlen argument stays `s`, so the copy is
  not provable and the loads stay. Probed 7 shapes in one compile with the score cut at 0x1cac3,
  before the shared epilogue (GOTCHAS "copies of one function share their identical tail").
- **A constant the game stores to a global through a register comes from an `int` local assigned
  inside that arm** (iter 17, [generic]): msgbox 0x2a45c materialises the default row as
  `mov eax,0x5c` (inside the arm, after the `test si,si`/`jne` at 0x2a457) + `mov word [spr_y],ax`,
  where `spr_y = 0x5c;` and every ternary give the 7-byte `mov word [spr_y],0x5c` (GOTCHAS iter 11,
  "write `if`/`else`, not a ternary", covers the other direction). `int row; row = 0x5c;` *before*
  the if is hoisted above the test (2 diffs); written inside the then arm it stays where the game has
  it and msgbox MATCHes (148 -> 152 bytes, 4 diff rows -> 0). A `short` local folds into the store and
  does not work, `int` (and `unsigned`) do. `int t; if (c) spr_y = (t = 0x5c);` gives the immediate
  form. Probe lesson: reordering the *unchanged* statements of the probe body (the four spr_x/w/h
  stores) re-scheduled everything and moved the control score 3 -> 9, so copy the committed body
  verbatim into the probe and vary only the block under test.
- **One local used in two switch arms has ONE home register; two locals get two** (iter 19,
  [generic]): com_isr 0x106c8 keeps the receive byte in EDX (`mov edx,eax` 0x10716, store from DL
  0x10726) and the transmit byte in EDX too (`movzx dx, byte [eax+0x80afa]` 0x10771, `movsx eax,dx`
  0x1079a before the port goes back into EDX at 0x1079d), so the dead `txcount` load takes AX
  (0x10779). Ours spelled the two arms with separate locals (`rxbuf[rxhead++] = inp(comport);` and
  `out = txbuf[txtail++];`) and the second one got EAX (`movzx ax` + `cwde`), swapping AX/DX - 6
  rows, and every type of `out` (short/unsigned short/int/char) left it there. One `short b;` read
  by the transmit arm and written by the receive arm (`b = inp(comport); rxbuf[rxhead++] = b;`, the
  same DL store the inline form gives) homes it once, the receive arm's copy pulls the home to EDX
  (EAX is the index there), and com_isr MATCHes. Look for a shared local before writing an
  EAX/EDX pair off as the coin flip above; and where two switch arms touch the same kind of value,
  one name is the fact the bytes show. com_isr_fifo 0x10810 (its `short out` plus the implicit
  receive temp -> one `short b`) MATCHed the same way minutes later, so the lever is not a fluke.
- **A shift kept in the local's own register comes from a compound assignment, and it picks the
  registers too** (iter 19, [generic]): pal_shift 0x114b0-0x114b9 is `movzx dx, byte [eax]` +
  `shl edx,2` (a 32-bit shift of a 16-bit-loaded value) + `mov byte [eax-1], dl`, with the loop
  counter in EBX. Written as the obvious `*p++ = v << 2` 9.5b keeps the value in EBX, shifts only
  the byte (`shl bl,2`) and puts the counter in EDX — 6 rows, and it reads like the coin flip of
  the two entry registers. It is not one: `v = *p; v <<= 2; *p++ = v;` (score 0 in a 14-variant
  probe) gives the game's registers *and* the whole-register shift, because the shifted value stays
  live in the local's home instead of being consumed by the store. Before logging a counter/temp
  register swap, try the two-statement form with the operator assignment.
- **Watcom's cold-tail duplication depends on source line layout, not only on the syntax**
  (iter 19, [generic]): in a two-function probe, com_isr's merged-local form written with braced
  `if (rxhead >= 0x4000)` / newline / `rxhead = 0;` came out 338 bytes with the `done:` epilogue
  emitted twice (once fallen into from each `default`), and the same statements on one line came out
  328 with the game's single epilogue - textually identical programs. In the full 19k-line module the
  braced form is the one that gives 328. So a probe's byte size is a property of the probe file, not
  of the function: rank shapes in a probe, decide in `src/ww.c` (GOTCHAS "the probe's scores are not
  the whole-file ones").
- **One data object can carry two typed C names for one link name** (iter 21, [generic]):
  `extern short w7ed8c;` + `#pragma aux w7ed8c "_v7ed8c";` next to the `extern int v7ed8c` the rest
  of the file uses (the same trick the project already uses for `b88cf2` - `#pragma aux` renames
  data, not only functions). Use it where the game truncates through a *word* load:
  `v88cae = w7ed8c & 0xff;` gives `mov ax,[0x7ed8c]; xor ah,ah; mov [0x88cae],ax`
  (net_key 0x2dd42-0x2dd4e). The same `& 0xff` written on the `int` name compiles 32-bit
  (`mov eax,[g]; and eax,0xff`) and re-shuffles the whole function (11 more blocks). Retyping the
  shared extern itself is not an option: every other reader changes too.
- **An assignment written before a call is scheduled early; sinking it below the call moves the
  load to the last slot before it** (iter 21, [generic]): the game loads `'Z'` into ESI at the last
  slot before `call net_send` (net_key 0x2dd8a), ours 0x1e bytes earlier; `net_send('D', key, flag);
  send = 'Z'; state = send;` - legal because the call does not read `send` - moved the `mov esi,0x5a`
  to the game's slot and net_key 0x2dd30 went 8 diffs -> MATCH (baseline 190). The load cannot sink
  past the call itself: the value is an argument register (`mov ecx,esi` at 0x2dd99), so "just
  before the call" is the only later home.
- **A tail-padding row is often the same register choice as the row above it** (iter 21, [generic]):
  `com_wait_byte` 0x2df70 ends with our `lea eax,[eax]` where the game has `nop`. Not a padding bug:
  the game's elapsed temp is ECX, so its `mov ecx,[0x...]` is the 6-byte `8B 0D disp32` while our
  EAX form is the 5-byte `A1 disp32` - the 1-byte gap shifts the tail and the padding absorbs the
  difference. Same for `cmp`/`mov` with an absolute operand: an EAX-only choice shows up as TWO fndiff
  rows (the instruction, plus the tail). Do not spend variants on the padding row.

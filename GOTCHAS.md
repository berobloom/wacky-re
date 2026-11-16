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
  as understood, mark `differs:`, and move on - it is question 1/2, not a missing statement. [generic]
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

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

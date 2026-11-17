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

Rules only: each entry is the rule plus one piece of evidence (an address). The per-function story
(what was tried, row counts) lives in that function's `differs:` note in `src/ww.c` and in git
history. Before adding an entry, grep this section and extend the matching one; tag entries that are
not about this game `[generic]`. Watcom = WCC386 9.5b with the project's flags throughout.

### Reading a near-miss diff (do this before spending variants)

- **difflib's autojunk inflates row counts on long functions.** `SequenceMatcher`'s default ignores
  lines that make up over 1% of a 200+ line list (`call @`, `mov eax,@`), so whole blocks misalign:
  fn_17928 counted 171 rows with it and 35 without, race_run 697 vs 213, and a harmless edit to main
  read 80 -> 192. Turning it off is not always better (netkeys_in 29 -> 48), so `fndiff.align()` takes
  the smaller of the two; fndiff, variants.py and progress.py use it (iter 39). Use it in any new
  scorer. [generic]
- **`src/check.sh` is the verdict, every other count is a hint.** `fndiff.py` (ours LEFT, game
  right) and `fncompare.py` print displacements as `@`, so swapped stack slots show 0 rows while
  check.sh says "differs" (net_send 0x2dc38). A MATCH masks fixup bytes, so read the `WRONG sym@addr`
  lines after it too (fn_1c88c loaded save_c for save_a). [generic]
- **Fake rows to subtract first:** a `[reg - 0x6f6f6f70]`/`[reg +/- @]` pair is a masked fixup
  (0x90 filler) - 7 of self_project's 10 rows; rows past the end of a shorter function are its own
  epilogue or alignment (race_records_menu, ctl_label's nops); a padding row (`lea eax,[eax]` vs
  `nop`) is the size gap of a register choice above it (com_wait_byte 0x2df70). [generic]
- **Rows that are only stack displacements = the slot-permutation dead end** (below): count them
  first (showlines 0x2a268: 10 of 18). [generic]
- **Equal instruction counts but a byte compare that starts on a branch displacement = a register
  home, not a missing instruction.** AL/EAX own the short absolute forms (`a0`-`a3`, 5 bytes; any
  other register 6, `add dl,imm8` 3 vs 2), so one home shifts every later jump (shots_move 460 vs
  462 bytes at 0x2613b). Sum instruction sizes per side to find the store. [generic]
- **Line up the two listings' loads before probing:** load ORDER differs = 9.5b's scheduler, only a
  statement reordering can move it (net_input, fn_16ca0 fixed that way; fn_19db8's switch
  controlling expression is not a statement: SKIP). Order equal, homes permuted = the colourer, try
  the levers below, then SKIP. [generic]
- **Read the width of a compare:** `cmp r16,r16` = both shorts in C, `cmp r32,r32` = at least one
  int. Fix the C types before logging a register pairing (fn_16eac: `int cnt` 17 -> 11 rows). A
  `short` that the game keeps in a register may take a stack slot in ours and the reverse
  (fn_1c0f4 vs scores_screen): try the type before restructuring. `xor ah,ah` before `cmp ax,N` says
  `short`, not `unsigned char` (com_open's `iir` 0x10401: 33 -> 19 rows). [generic]
- **A lower row count can be a worse function**: a variant that drops one of the game's
  instructions realigns the rest (shots_clear lost `push ebx`, scored 11 vs 13). And a variant can
  print the game's text while reading the wrong value (line_draw: dx/dy rotation moved above its
  read). Check register by register that each def reads what the game's def reads. [generic]
- **Function boundaries:** a `ret` is not an end (kart_hit continues at 0x2175f after one at
  0x2175e: run `xref.py` on an "after-ret" address); no caller is not "not a function" either
  (0x1c88c and 0x1c968 are dead code with their own prologue/ret/padding). Decide from
  prologue/ret/padding. [generic]

### Probes (`tools/wc/variants.py`)

- **A probe file must hold what the function sees in src/ww.c:** `#include <string.h>` (else real
  strcat/strlen calls), the same externs, and the DEFINITIONS of game functions it inlines (a
  prototype only gives a smaller function: com_wait_byte 80 vs 152 bytes). Confirm the control
  variant's size and score equal the whole-file ones before trusting a ranking (com_expect: equal
  -> rankings transferred). [generic]
- **Copies in one probe share their identical tail** (9.5b merges epilogues across functions):
  cut the score range before the shared tail (demo_mode 0x1e69f). Both sides are cut at END since
  iter 16; re-run older probes. A `variants.py` score can be 1 above `fndiff.py` from the variant's
  own filler: compare variants with each other at equal size. [generic]
- **Rank in a probe, decide in `src/ww.c`:** reordering unchanged statements re-schedules the whole
  probe (msgbox control 3 -> 9), line layout of the same tokens changed a probe's size (com_isr 338
  vs 328 bytes), and a probe's best spelling can lose in the full file (line_draw 4 -> 13). Copy the
  committed body verbatim and vary one block. [generic]
- WCC386 runs in DOSBox: probe file names must be 8.3 or it fails silently (no OBJ). [generic]

### Levers that produced MATCHes (try these before SKIP)

- **The game CALLS a function our -oe inlines:** call an alias at those sites,
  `void f_x(int); #pragma aux f_x "f_"` (com_getc_x, waitkey_x, makepath_x, setvmode_x, ...); for
  a function called everywhere `#define readdat readdat_x` after its definition. Where the game
  DOES inline the real seekdat/readdat (effects_load 0x2754c) `#undef` them around that function.
  Defining a small game function lets -oe inline it into callers that already match (fn_1c068 broke
  net_end): xref the callers first. Nothing turns -oe off per function. [generic]
- **The register of a local follows its first DEFINITION, and among pointer temps the declaration
  order:** move the first assignment (classes_menu nrows got EDX 0x19f22); `d = q + q; c = base;
  a = c; b = c + d;` with c declared first gave track_pics_load's five instructions. [generic]
- **Route a temp through a local that already lives in the wanted register:** `n = i * 12;
  f(path, (char *)names + n);` (n later takes datsize()'s EAX) fixed songs_load's EAX/EDX swap.
  [generic]
- **Split the arithmetic with a compound assignment so the value stays live in its home:**
  `v = *p; v <<= 2; *p++ = v;` (pal_shift 0x114b0, whole-register shift + the game's registers);
  `m = v88b88 - 1; m *= 0xf3c;` with m declared first (track_load 0x27a49 `movsx ecx` + `dec ecx`).
  [generic]
- **A slot swap with a pointer the compiler made from `a[i]`: make the pointer an explicit local,**
  declared BEFORE the scalar it swaps with and assigned where the game first stores it
  (`char *nm;` ... after the preceding call `nm = names[0];` ... `nm += 12;`): sounds_load 0x12124,
  0x168/0x16c, MATCH. Declared after, or assigned earlier, keeps a swap. [generic]
- **One local shared by two switch arms has one home, two locals get two:** one `short b` for the
  receive and transmit byte fixed com_isr 0x106c8 and com_isr_fifo. [generic]
- **A 0/1 value in its own home: braced `if (c) { x = 1; } else { x = 0; }`**, not `x = (c)` (which
  computes in a scratch + `mov`): ctl_label 0x19d0e `sete al; movzx eax,al`. The `? 1 : 0` ternary
  form also changed ents_show's whole loop shape: braced, 9.5b emits the game's `jmp` to a bottom
  test with one epilogue (0x14393) instead of a top test + duplicated epilogue (49 -> 36). [generic]
- **An int stored as a dword slot and read back `movsx r, word [slot]` = an `int` local passed to a
  `short` parameter** (or `(short)n`): duck_screen's three centred x values are an `int w` (game slot
  0xc8), its loop keeps `short x` (cwde); screenshot's file counter is `int n` with `itoa((short)n)`
  (51 -> 6). Scan: `mov dword [esp+N],r` followed within 4 instructions by `movsx r2, word [esp+N]`.
  [generic]
- **`(int)` casts on both arms of a short ternary index** give per-arm `movsx` with no `cwde` after
  the join (draw_status `pnames[v7eb80 ? (int)myplayer : (int)v88c6a]`, 48 -> 30). [generic]
- **A `je` straight to the epilogue is an early `return`, not the end of an `if`:** duck_screen
  0x2bad6 `if (!after) return;` - the rebuild had run the PRESS FIRE/fade tail for both callers. Check
  every conditional jump whose target is the epilogue against the C before probing codegen.
  [generic]
- **Raw-address macros (`WS(0x6ab14)`) cost rows:** 9.5b loads the constant into a register and
  stores through it, the game addresses the symbol (`mov word [players+0x84],bx`). Rewrite with the
  struct fields of the right global (fn_16d68). [generic]
- **A constant stored to a global through a register = an `int` local assigned inside that arm**
  (`int row; ... if (...) { row = 0x5c; spr_y = row; }`; a short local or a ternary folds to an
  immediate store): msgbox 0x2a45c. [generic]
- **A conditional flip of a global = `if (g == 0) g = K; else g = 0;`, never a ternary** (the
  ternary is a value select through EAX; the game never materialises 0x3e80 in a register:
  55 `mov word [m],0x3e80` sites, 0 `mov eax,0x3e80`): cars_menu, enter_name. [generic]
- **Load order:** write a load the game emits early as an earlier statement (fn_16ca0 0x16cf9:
  `i = v7ea58s;` before `v88c7a = v88c7c >> 1;`), and sink an assignment the game loads late below
  the call that does not read it (net_key 0x2dd8a `send = 'Z'` after net_send). Independent bit
  tests in the game's load order (net_input). [generic]
- **`volatile`:** a `volatile int t` local gives the game's `sub esp,4` slot and `[esp]`
  spill/reload (shot_step 0x25f43); do arithmetic on it in its own statement (`k = last - 1;`,
  drawsprite_clip 0x11416). A `volatile` alias of a global pins a deferred store
  (`extern volatile unsigned char v88ccav; #pragma aux v88ccav "_v88cca"`, demo_play 0x2e35f) and
  restores a re-test the game repeats after stores (race_results); alias per function, a volatile
  global changes other functions. [generic]
- **A second typed name for one global** (`extern short w7ed8c; #pragma aux w7ed8c "_v7ed8c"`): a
  word load + `xor ah,ah` truncation (net_key 0x2dd42); int aliases of char* globals stop the
  scheduler hoisting a pointer chain (data_load 0x28180). No alias needed for the same effect: write
  `(int *)charbase + i * 2` where the address is the same as `(int *)(charbase + i * 8)` - the cast
  around the whole sum keeps the sum's load late and re-pairs the index registers (track_around
  0x283a4, 31 rows -> MATCH). [generic]
- **Chained assignment computes through a scratch:** `k = g = x` loads into AL; `k = x; g = k;`
  loads into k's home (demo_play). `t = dx >> 1; err = t;` computes in EAX and moves, `err = dx >> 1`
  computes in place (kart_line 0x21de0) - which one the game used varies per function. [generic]
- **Loop shapes:** a tail-tested loop with its end from the variable, `end = p + N; do {...} while
  (p != end);` (track_load .SIN `lea edi,[esi+0x360]` 0x27d80); a guard clause `if (skip) { p++;
  continue; }` with the step at the body end keeps the game's tail order where every `for (...; p++,
  i++)` is re-sorted (anim_tick 0x261fc); a pointer re-loaded twice in a loop needs a second local
  re-armed at the body end (fn_1c994 0x1ca83); a retry loop is `while (1) { if (x != 0) {...return;}
  if (y != 0) break; }` (answer_call 0x1e0f4); 9.5b unrolls constant-count loops itself, write the
  plain `for` (crack_draw). [generic]
- **Control-flow shapes:** a cold `then` block after the chain = `if (!c) { big } else small;`
  (classes_menu 0x19f61); a 3-case `switch` on a short is a compare tree with cold blocks
  (laps_menu 0x1a148), not an if-chain; `if (c != 4) { ... } else x = 6;` shares a store block that
  an early `return` duplicates; repeated `store; ret` tails are one shared `store; return;`
  (shot_hit 0x218a4); a compare `movsx r,[m]; cmp r,N` is `k = m; if (k >= N)`, a direct `cmp word
  [m],N` is the field compared in place (ents_flags 0x213f0). [generic]
- **An asm routine's prototype comes from its own prologue, not from what callers load:** f375cd
  0x375cd is `pushal ... popal; ret` (0x376d5) and reads p7d714, so it is `void f375cd(void)` (all
  registers survive). The "EAX/EDX arguments, result in DX" were the caller's `p7d714 = p` value and a
  `short` save of pal_a held in DX across the call: `p7d714 = p; fs = pal_a; f375cd(); pal_a = fs;`
  (0x1ce57/0x1ce6c). Retired the old "16-bit EDX argument" dead end; credits_screen 44 -> 32. [generic]
- **A ternary as a call argument blocks 9.5b's strength reduction of the loop index**; the same
  call in an `if`/`else` lets it replace `k` by stepped pointers/offsets like the game
  (kartsets_load 0x28038, 48 -> 36). [generic]
- **Strings:** a byte-loop strcpy is `strcpy(buf, &"LIT"[0])` (keeps the literal in CONST; songs_pick
  0x32e60); `strcpy(p, "")` is a 1-byte copy of the pooled "" at 0x600ac (the NUL of "WACK").
  Watch check.sh's `segment CONST` line after every new string: one base = literals in place.
  [generic]
- **A near miss can hide a behaviour bug the row count never shows:** a wrong field (cboxes[1].x2
  for .x), a swapped string, a store into the wrong table, or code made unreachable (no label after
  a `goto`, which 9.5b drops without a warning: fn_1832c lost its joystick path and trial_book
  call). `tools/wc/nmrefs.py` compares what both sides reference; run it after every change. [generic]
- **An unsigned `cmp eax,1; jbe A` + `cmp eax,2; je B` + `jmp` chain is a `switch` with `case 0: case
  1:`**, not `if (v <= 1) ... else if (v == 2)` (signed `jg`, other block order). A block the game
  jumps BACK into from a later test is the same statements written twice in the source and
  cross-jumped: `if (netgame == 0) { A } else switch (v) { case 0: case 1: A; break; case 2: B; }`
  (fn_1c0f4 0x1c538-0x1c58b, 67 -> 14). [generic]
- **A row loop over `char rows[N][K]` from 1: an explicit `char *r` declared last and stepped in the
  for header** (`for (i = 1, r = rows[1]; i <= n; i++, r += K)`) took volume_menu 0x1e9cc 64 -> 48
  and fixed the scalar slots after it; declared first or set in the body, no gain. [generic]
- **Stores near an inline strcpy slide into its setup unit**; which ones is a source-position fact,
  probe every position (classes_menu 0x19f01-0x19f27). [generic]

### Dead ends: SKIP at once (0 variants) unless you bring a new idea

- **Stack-slot permutation of scalar locals** (QUESTIONS 9, 10), unless one of the swapped slots
  is a compiler-made pointer (lever above): neither declaration nor first-use
  order (arrays DO follow declaration order, ascending from esp+0). 43 orders in enter_name, also
  release_check, wtext, scores_screen, fn_1c0f4, showlines, cars_menu. Size a local array from the
  frame, not its largest index (scores_screen `long sc[10]`); never-used locals are dropped, so a
  frame gap means the game used those slots. The reverse too: an over-sized buffer inflates the frame
  and every slot displacement (`char buf[8]` gave time_str 0x2a61d a 0x14 frame where the game has
  0x10; `buf[4]` fixed it, 33 -> 31 rows). [generic]
- **A global kept in a register across a loop** (load at entry, store before each call and at exit)
  is NOT a dead end: the game's source used the global itself as the working variable. Drop the
  local copy and compute in the global (`spr_x = p->x - 0x400; if (spr_x < 0) spr_x = 0; ...`):
  minimap 0x2a4ac 44 -> MATCH, fn_16eac (`n = nplayers` copy removed) 11 -> MATCH. Signature to
  scan for: `mov [G],r ... call; mov r,[G]` in the game - the store can sit up to a dozen
  instructions before the call (shot_fire 0x2d5ad: v7d874 walks the shot table, 57 -> MATCH;
  fn_16d68: `v7d7f8 = &trklines[k]`, stored once at the exit, 58 -> 26). A global that is already
  the working variable and still is not cached (players_ahead's v7d800) is unexplained. [generic]
- **Which of two locals gets EAX, or which of two pushed registers** (QUESTIONS 1/2, the "coin
  flip"): fn_19db8, pcx_load ESI/EDI, fade_in, netkeys_out/pack/in (c in DL), net_recv crc, shots_clear,
  com_wait_byte, com_expect, self_project, drawsprite_clip, engines_menu (the int that reaches a shared
  tail: game EDX, ours [esp+8]; `short` wins EDX but every op turns 16-bit). Check the width rule and
  the levers above first. 9.5b has NO explicit register variables (`@ edx` is E1009 in a function,
  E1059 at file scope), and declaration order of globals or comma-`for` initializers is not a lever.
  A loop-invariant literal argument is the one exception that moved a home (fn_16f24, 39 -> 26). [generic]
- **Scheduling the game does differently in one straight-line run:** a zero argument constant -oe
  sinks below inlined stores (music_jingle, duck_screen, race_results, trial_book: `xor edx,edx`),
  one swapped pair of loads (track_setup 0x124e8), a store 9.5b hoists or sinks out of a group
  (classes_menu 0x1a05d, QUESTIONS 3), two independent load chains interleaved (in_sector 0x24a08; a
  statement written below a branch sinks past the whole branch), a dead copy of an incoming register
  (fn_182d0 `mov esi,ebx`), an allocator spill `mov [esp],eax; mov edx,[esp]` with no room for a
  local (view_at_curpos), a param a copy into a local will not spill the way the game does (its
  mirror image: the incoming value lands in the new slot at the prologue and the register is
  reloaded after the call - fn_1d704 0x1d7b5, 11 spellings). `-3` vs `-3r` is not the cause
  (identical object). [generic]
- **Address constants 9.5b folds:** `&netstats[1]` as one immediate vs the game's `mov esi,0x3c;
  add esi,0x7c818` (showlines 0x2a359). But `mov r,N*stride; add r,sym` in a loop names an array
  base one row earlier - declare it (`mrows` 0x7bc00, trial_records_menu) before writing it off.
  [generic]
- **Store->load value forwarding:** `v->f0a = d; x = v->f0a;` compiles to a register copy before
  the store, no reload; a `(volatile short *)` cast on the read does not force one (9.5b forwards
  anyway). The game's `mov word [ebx+0xa],dx; mov cx,[ebx+0xa]` (kart_catchup 0x28bf8) is not
  reachable; a partial store breaks the form. SKIP. [generic]
- **Extra bytes from a larger frame for one more local** cost spills and re-colour unrelated temps
  far away (kart_draw 17 -> 28; track_load's v88b88 load): score the whole function. [generic]

### General rebuild facts

- **`src/check.sh` after a compile error scores the PREVIOUS object** unless it deletes it first
  (fixed iter 36: `rm -f .work/src/WW.OBJ`); grep its output for `Error!` before reading a count.
  [generic]
- Structs are pack(1) (`imul 0x6a` where the game has `0x68` = natural packing). [generic]
- Literals land at the game's CONST addresses only when functions are added in address order.
- A switch's jump table sits in the gap BEFORE the function (0x1a360 in cars_menu), table order is
  source-label order, and case bodies are emitted in source order: sort the cases by jump-table
  target before writing the switch (main_menu case 7 before case 6; 0x19da8 for fn_19db8).
  [generic]
- Read esp offsets per instruction: a 5-argument call pushes one dword, so displacements after a
  `push` are 4 lower (cars_menu 0x1adf7). [generic]
- A call target can be an interior label of a library module (0x430f7 inside `__stos` 0x430be):
  declare it as its own extern with the call site's registers. [generic]
- A short parameter whose callers do not sign-extend: the callers had no prototype - declare
  `void f();` and define with `short` (fn_1d704). Call-site argument count must match the
  definition (E1151): pass a callee-ignored extra argument through an aux-pragma'd parameter list
  (voice_play 0x18da1). A stack slot incremented but never read is a parameter used as the counter
  (`first++`, credits_page 0x1eee4). [generic]
- `#pragma aux f value [dx]` is accepted where `value [edx]` is E1121; the prologue's `push ebx`
  means a value lived in EBX, not an ABI rule (ctl_label). [generic]
- Pinned -o letters from single-function probes can be wrong for the whole program: re-check every
  flag against the full MATCH count (com_open needs `e`). [generic]
- Two-digit padding: `buf[1] = buf[0]` before `buf[2] = 0` (bigdigits 0x2b558; not every site).

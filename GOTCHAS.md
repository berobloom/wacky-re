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
18. **A whole-screen mean RGB difference cannot see a moved widget**
    [generic]. `tools/link/racetest.sh` compared one screen with the
    original's mean (threshold 20): the `menu` 0x155c0 sponsor-banner bug (y
    from a literal, not the caller's c, 0x15fad) read 9.5 with the bug and
    0.7 fixed. Count the pixels that differ by more than 40 as well (10.8%
    with the bug, 0.73% fixed) and FAIL on either, and check the screenshot
    actually shows the changed widget: the driver script's comment said the
    engines menu appears before the 7th Enter, the image said class.

## This project's traps

- **`overnight/cleanup.sh` kills the shell that called it when that shell's command line names the
  harness driver** [generic]. Its pattern `[d]osrun\.py` keeps pgrep off its own line but matches any
  `bash -c` whose text contains the driver's file name - a heredoc body, a `find -name`, a loop that
  runs the driver: exit 144, no output (iter 74, three times). Put such steps in a script file
  written beforehand and run that; never name the driver in the same command as a cleanup.

- **An absolute operand with no fixup is a bug, however identical its bytes.** Our
  `WI(0x6e6e0 + edx)` in `fn_17928` was byte-for-byte the game's `mov [edx+0x6e6e0],eax` 0x17a3a,
  and iter 65's globrefs pass read exactly that ("an absolute disp32 with no fixup on it") as SAME.
  The game has a fixup there; DOS/4GW loads DGROUP away from its link base, so ours wrote the raw
  link address and the race place icon read a NULL table (QUESTIONS 25, two sessions). Run
  `tools/link/fixcensus.py` after `tools/link/build.sh`: any data target the game relocates and the
  hybrid never does is such a literal (it found six more: `fn_173c4`, `fn_16f90`, main). Spell
  game addresses as `litNNNNN` (src/ww.c:15) or a `#pragma aux` alias, never as a number. A
  `difffn` run does not catch it either: it loads both images at their link base. [generic]

- **Compare behaviour, not bytes: run the function.** `tools/wc/difffn.py` calls one function in
  `original/WW.EXE` and in the hybrid build (`.work/link/WWNEW.EXE`, leparsed to `.work/le/wwnew`),
  each on its own image, and compares every byte of the data segment plus the zero page, EAX, and the
  library calls. It found `players[].f14` stored as 0 where the game stores `abs(v88c04 - x)` - one
  store, same address, same instruction on both sides: no row count, address census or constant
  census can see that (iter 69, findings/game-loop.md). Traps of the tool itself: one function per
  process (a second `Uc` in the same Python process dies with "Could not allocate dynamic translator
  buffer"); the library stub range is the mirror image in the two builds (above the game's C module
  0x36a4e in the original, below `_TEXT` in the hybrid) or a helper runs in one build and is stubbed
  in the other; a run stopped by the instruction cap is at whatever progress its own code reached, so
  a DIFF there needs re-running at two caps before it means anything; the DPMI zero page is mapped and
  compared, which turns a null-pointer crash into data that can be diffed; and its no-argument sweep
  selects the open lines with `'MATCH' not in line`, so a line whose note only *quotes* another
  function's status is skipped - iter 70's one real bug (menu 0x155c0's msgbox y) sat on the
  `engines_menu` line the sweep never printed, and was found by naming that function explicitly; and
  a DIFF byte that is a **stack address** is not behaviour - `palptr` 0x7d760 ends scores_screen
  0x1ce14 pointing at fade_in's local palette buffer (0x32d4c `mov dword [0x7d760],eax`), which sits
  4 bytes lower in our build because the frames are a different size, while the 0x800 bytes below it
  are byte-equal (iter 70: compare what the pointer points at before calling it a BUG). A **capped**
  run needs a seed, and the seed has to be what the loop cannot get by itself: `SEED=addr=value[:b|:w|:d]`
  writes globals into both images, `TICK=n` adds n to `ticks` 0x7e720 per stubbed call because the
  timer ISR is stubbed (iter 75). A static seed can never exit a wait that compares a counter with a
  snapshot of itself (menu 0x16057 `jl 0x1604c` after 0x15bd9 stored `ticks` into 0x7ea70), and it can
  set an input flag but never release one - the release spin 0x16286 `cmp byte [0x88cd0],1; je 0x16286`
  is unseedable, so look for the branch that returns without waiting (0x16331). The control run needs
  the function to be a public in `.work/link/WWORIG.MAP`: a static original (`com_wait_byte` 0x2df70)
  has no control. To find the loop a capped run sits in, `FN=name .work/venv/bin/python
  tools/wc/hotfn.py` counts executed addresses in the original image and marks the backward jump - it
  named menu's frame wait 0x1604c and com_wait_byte's ring wait 0x2df7b in seconds. A hand-built
  `difffn.Run` for the **hybrid** must use `main`'s stub range (BLOBTEXT below `_TEXT`), not the
  original's `(0x36a4e, inf)`: the wrong one leaves a different build's library live and invents
  behaviour (iter 75: com_wait_byte 0x2df70 looked like EAX 0 plus one phantom stubbed call).
  A run whose `not done` and `not calls` is **not necessarily at the instruction cap**: `emu_start`
  also returns quietly at an instruction Unicorn cannot execute (`in`/`out`, i.e. Watcom's inline
  `outp`/`inp`) or at an unhandled CPU exception (`div ecx` 0x103b1 with `baud` = 0), and difffn
  printed both as "the 3000000-instruction cap" - com_open 0x102f4 reported that after 27k
  instructions in 0.2 s (iter 76: `UC_HOOK_INSN` on IN/OUT plus the new `ARGS=` register arguments
  fixed it). A `SEED=` **pointer** has to point at a block that is zero in BOTH images: the hybrid's
  data object is 0x35464 bytes against the game's 0x33100 and carries its own initialised data, so
  0x92000 is zero in the game but `+++.OK` at 0x92126 in ours, which invented a 5-byte `kart_draw`
  DIFF (iter 76). A fault address just **below** the zero page (`read at 0xfffffffd`) is not a null
  pointer but a **negative index**: a zero global run through a `dec` and an index multiply
  (`lea eax,[edx*4]; sub eax,edx` = *3) lands under the mapped page, so seed the source global to a
  legal value rather than moving the base (`SEED=0x88c96=1:w` for
  `pal7d848 + 3*(v88c96-1)`, iter 79, findings/menus.md).
  Recipe and its evidence: findings/menus.md, findings/serial.md, findings/game-loop.md.
- **A capped `difffn.py` run is only as good as the progress its cap allows**, and `hotfn.py`'s
  `capped` does not say which limit fired (instruction cap, `CALLS`, or the quiet stop at an `in`/`out`
  or a fault), so read the stop point yourself: `Run(...).go(addr)` then print `done`, `calls`,
  `ncalls` and EIP - that is how `fn_1c0f4` (CALLS=300, stop 0x27969 inside `wait_input`) turned out to
  be an input pump rather than a stuck loop (iter 77). Two rules for the seeds:
  `TICK=n` fires **only at a stubbed call or interrupt**, so it cannot end a wait whose body has no
  call (`credits_screen` 0x1f305, `showlines` 0x2a3bd, `intro` 0x34cb4) - seed the global there, or
  size `TICK` to beat the deadline in one add when the wait zeroes the counter first (`TICK=65536`
  clears the word deadline 0xbb80 after 0x1f05a zeroed `ticks`, `TICK=8` needed > 2000 calls), or hook
  the clock itself: **`TICKREAD=n`** adds n on every READ of 0x7e720, which reaches a call-free spin and
  is a *data* hook so it works on both builds' copies of the loop at their different addresses
  (`chat` 0x150e5, iter 79); it counts **reads**, so a build that polls the clock a different number of
  times per pass grows it differently - check the read counts before calling a tick-dependent DIFF a
  bug, and prove the fast clock did not skip the body by re-running at `TICKREAD=1`; and
  `SEED=` is applied once before the run, so a press is seedable and a release never is
  (findings/menus.md: `wait_release` 0x278c4 with mode 0 returns only when `v88cce` and all four
  joy flags are 0, and stores joy88b40=1 itself when `v88cce` != 0) - **`RESEED=0x88cce=1:b`** now ends
  that class: same syntax as `SEED=`, but it writes the value on every READ of those bytes except the
  first, which gets `fn_1c0f4` 0x1c0f4 to return on both sides (findings/menus.md, iter 81). The
  first-read exception is not decoration: the READ hook fires **before** the value is fetched and
  unicorn 2.1.4's python wrapper comments `UC_HOOK_MEM_READ_AFTER` out of its handler table
  (`hook_add` raises UC_ERR_ARG), so writing on the first read parks the run in the earlier wait. It
  counts reads like `TICKREAD` does, and it must be keyed on the global's address, not on stubbed calls
  - the two builds make different numbers of calls (`demo_mode` 87402 vs 95594 at the same
  instruction cap, iter 81). `CALLS=` is the useful lever for
  a screen that prints a lot: `credits_screen` returns between CALLS=2000 and 20000. For an argument
  register use `ARGS=` (eax, edx, ebx, ecx): `showlines` prints its three lines with `ARGS=3,1,0` and
  its mode argument decides which tail runs (findings/menus.md). A run **still capped at the end** with
  `TICK=`/`TICKREAD=` on reports the injected clock 0x7e720 itself as its only DIFF byte (game 08, ours
  10/0x18, `fn_1832c` at CALLS=3000/5000, iter 80): that value is where each side's cap fell, not a
  behaviour difference - a verdict needs a seed that makes both sides *return*.
- **A stubbed call that must return a different value every time makes its caller loop forever**
  (iter 81): `fn_1be3c`'s start-seat draw keeps only a candidate in 0..7 (0x1bec2 `test ax,ax;
  jl 0x1beae`, 0x1bec8 `cmp ax,7; jg 0x1beae`) and re-draws otherwise, so with the call stub leaving
  EAX = 0 the retry never ends and `fn_1be3c`/`demo_mode` look like a `CALLS` cap that no seed reaches.
  `SEED=` writes memory, not a return value, and `srand` is stubbed too so the seed the game asks for
  is never installed; the knob is **`RAND=n`**, which returns the image's own clib LCG (0x43092 `imul
  edx,[eax],0x41c64e6d`, 0x43098 `add edx,0x3039`) shifted and taken mod n - `RAND=8` draws exactly
  what that loop wants. `hotfn.py` names the class: the hot address is a `call` executed tens of
  thousands of times whose successor is a conditional jump back to itself, not a wait on a global.
- **A keyboard read is a return value too, so a name-entry loop needs `KEY=`, not a seed** (iter 82):
  `enter_name` 0x1cb44's input pump 0x1ccca-0x1cdff leaves only when the clib `getch` (game 0x42c4d) hands
  back 0xd or 0x1b (0x1cd8f/0x1cd97), and its body reads no global at all, so `SEED=`/`TICK=`/`RESEED=`
  cannot move it and the stub's constant 0 loops forever. The knob is `KEY=c1,c2,...` in `difffn.py`: the
  `getch` stub returns them in call order, the last one repeating, so the run always ends (and the
  character list walks the accept ranges - upcase fold, digits, space, backspace). Same trap on the tool
  side: `hotfn.py` ignores `KEY=`/`FREAD=`/`RAND=` unless that image's address is wired into its `Run`
  too - `fread`/`rand`/`getch` are looked up in `difffn.main()`, which `hotfn.py` does not call, so a new
  stub knob needs its `r.<name> = D.<name>_addr('ww')` line added there or the env var silently does
  nothing and the "capped" verdict means nothing.
- **One flag can be both the press and the release, and `RESEED=` on it is what ends the pair**
  (iter 82): `fn_1d704` calls `wait_input` with mode 1 at 0x1da77 (returns EAX=1 only while `key88cd0` 1,
  0x27984 -> 0x2798d) and then waits for that same byte to be 0 at 0x1daaa `cmp byte [0x88cd0],1;
  je 0x1daaa`, whose body calls nothing - `SEED=0x88cd0=1:b` alone burned 749369 passes there inside the
  3M-instruction cap. `SEED=0x88cd0=1:b,RESEED=0x88cd0=0:b` (first read sees the seeded 1, every read
  after it the injected 0) makes both sides return. Picking the wrong flag is its own trap:
  `RESEED=0x88cce=1:b`, which frees `fn_1c0f4`'s mode-0 pair, parks `fn_1d704` in `wait_release`'s pump
  because 0x278ef then stores `joy88b40 = 1` (19963 passes of 0x278ca-0x278ff). Worse in the other
  direction (iter 83, `trial_book` 0x2e4b8): seeding `v88cce` to free that function's pump puts the
  seeded 1 into `wait_release`'s test 0x278e7 first - 0x278ef's `joy88b40 = 1` store survives the
  injected 0 and the pump is never entered (2844 passes of 0x278ca-0x278ff). Check the hot-address list
  for who reads the global before picking it.
- **A capped menu is often spinning in `fade_out`'s palette pass, not waiting for the player: seed
  the fade step** (iter 84): the hot loop of that class is 0x1c03a <-> 0x32cc5-0x32cd7 (`movsx
  eax,bx; cmp byte [esp+eax],0; jle` / `inc edx; cmp dx,2` / `inc ebx; cmp bx, word [esp+0x300]`),
  which is `fade_out` 0x1bf94 walking 768 palette entries and re-marking them changed. The step
  `v88c46` 0x88c46 is 0 in an image that never ran `main` (the game stores 4 at 0x36334), so nothing
  ever converges. `SEED=0x88c46=4:w` ends it, and it is what `intro` 0x348f4, `demo_mode` 0x1e2e4,
  `race_records_menu` 0x1d13c, `volume_menu` 0x1e760 and `main_menu` 0x1f858 were stuck on (three of
  those five then return: findings/menus.md iter 84).
- **The `NEW=wworig` control is not SAME in a region that writes through a 0 pointer** (iter 84):
  `main`'s `malloc` is stubbed, so the palette pointers it stores (`v7d858` 0x7d858, `v7d85c`
  0x7d85c, `pal7d848/4c/50`, ...) are 0 and `pal_6bit` 0x26ec4 / `fade_in` 0x32cf8 scribble on the
  harness's 0x0-0x10000 zero page; the original and the relinked image leave different garbage there,
  so an unseeded `intro` run reported `DIFF 63 byte(s)` with `calls 1212/1187` **for the control**.
  Two rules: a DIFF whose addresses are all inside that scratch page is the dead-pointer class, not
  a bug (the loop counts it changes - a fade's step count, here 19 vs 6 - are consequences of garbage);
  and to make the control mean anything, move the pointer into the scratch page with
  `SEED=0x7d858=0x8000:d`, or give a register-argument function a real buffer with `ARGS=0x8000`
  (`difffn.py fade_in SEED=0x88c46=4:w ARGS=0x8000` -> SAME, both sides return).
- **Name a stub-call target exactly or invent a difference** (iter 84): `difffn.py CALLSEQ=1` (new)
  prints both stub-call sequences so a `calls 370/345` says which call our build skipped. Naming each
  address by the nearest symbol at or below it counted 22 unnamed 0x37xxx entries as `f37cf1` and
  made a 25-call gap out of an 11-call one, and our clib symbols carry a trailing underscore the
  game's hand names do not (`fopen` vs `fopen_`), so an exact hit prints the name and anything else
  prints `?nearest`.
- **A stub cannot fake a return from a noreturn clib entry: `exit_` ends the run** (iter 82): `main`
  0x35ca4 tail-jumps to clib `exit_` (game 0x42c11, hybrid 0x1c1d3) at 0x35e10, and the call stub's
  "return" pops the slot at ESP - inside main's live frame one of its own locals - so the game jumped to
  the code object's filler at 0x10000 (`cc eb fd` = `int3` + `jmp 0x10000`) and `int3` was recorded as a
  stubbed call 289 times. What `difffn.py` printed was `CALLCAP 300/70` and `one side never returned
  (game False ours True)`, which reads as "our main exits early" and is pure artefact: the counts are the
  number of int3 records each side racked up. The fix is to stop at the call (EIP = the run's sentinel,
  `emu_stop`), which makes `main` print SAME with 11 calls on each side; resolve the entry's address from
  each image's map (`exit_`) and re-wire it in `hotfn.py`.
- **A `void` function's `EAX x/y` note from `difffn.py` is not a DIFF** (iter 83): `race_run` 0x2fd8c
  returns from the abort arm 0x31eca with EAX = 1 in the game and 0 in our build. Its signature is
  `void FUN_0002fd8c(void)` and all four call sites in the decompile (lines 10774, 12023, 12119, 12183)
  call it as a statement, so EAX is dead and the line reads SAME on the data segment and the calls - a
  pass. Read the signature and the call sites before writing BUG.
- **A `SEED=` cannot reach a stack buffer, and a read stub that returns nothing makes a byte-wise
  parser loop forever** (iter 80): `read_line` 0x26ff0 stops on the byte it read, so with the clib
  `fread` stub delivering nothing it walks off `gam_load` 0x2717c's frame into unmapped memory; the
  buffer is a local (`mov eax, esp` 0x2724a) and the two builds' frames sit at different addresses,
  so no single seed fixes both sides. The knob is `FREAD=<byte>` in `tools/wc/difffn.py`: the stub
  writes that byte at its destination when the Watcom size register `edx == 1` (the byte-wise pattern;
  bulk reads of 0x80/0x300 stay untouched), and both `fread` addresses come out of the maps
  (`fread_` offset + that image's code base), never typed. Same run, a **zero global the game only
  ever sets in `main`** is its own trap: `v88c46` (the fade step) is 4 from 0x36334, and with 0 the
  fade loop 0x1c017/0x32ce2 marks every pass "changed" and repeats forever - seed it from the store's
  address, and say so in the note.
- **`SEED=` a global that a callee clears is no seed at all**: `SEED=0x7ecc0=1:d` makes
  `race_records_menu` 0x1d13c take its exit arm at 0x1d2f1 only if it is still nonzero there, and
  `menu` 0x155c0 stores 0 to it at 0x15613 and 0x16291 (write hook on 0x7ecc0, iter 77). Name the
  writer with a `UC_HOOK_MEM_WRITE` probe over `difffn.Run` before blaming the seed or the C.
- **`overnight/check.sh` runs `tools/*/test_*.py` with plain `python3`, not `.work/venv/bin/python`**,
  so a test that imports unicorn/capstone fails the check in a tree that compiles fine (`test_difffn.py`
  iter 76). Such a test catches ImportError and
  prints `SKIP:` with exit 0, and is run by hand under the venv. [generic]
- **`grep -rn` in this repository must exclude `overnight/runs`**: those session transcripts quote
  `src/ww.c`, `findings/*` and the queue verbatim, so one unrestricted grep returns the same text
  hundreds of times (iter 76: `grep -rn comports .` looking for one definition). Scope the search to
  `src tools findings analysis`, or use `--exclude-dir=runs`. [generic]
- **`tools/le/disasm.py` stops at the first undecodable byte**, so a whole-module census must be
  restarted from the next address: `disasm.py ww 0x10000 0xbad9` ended at 0x1ffff after 15506 lines and
  the file reads complete - it made iter 70 write "38 `msgbox` call sites" where the C module has 53
  (chunk it: `disasm.py ww $a 5000`, resume at the last address + 1).

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
- **A function out of address order can own another's jump table.** fndiff and check.sh measure a
  function up to the next public, and Watcom puts a switch's table in front of its function, so a
  function standing before a switch function in `src/ww.c` but not in the game counts that table as
  its own rows: ent_sprites_load (0x21310) stood before main_menu and read 75 rows, 43 of them
  main_menu's table, called "padding" for 40 iterations; in address order it is 32 (iter 56c). Check
  that a near miss's neighbours in the source are its neighbours in the game. [generic]
- **Fake rows to subtract first** (iter 39: fndiff, variants, permute and nmrefs now fold the `- @`/`+ @`
  pair themselves, so their counts no longer include it): a `[reg - 0x6f6f6f70]`/`[reg +/- @]` pair is a masked fixup
  (0x90 filler) - 7 of self_project's 10 rows; rows past the end of a shorter function are its own
  epilogue or alignment (race_records_menu, ctl_label's nops); a padding row (`lea eax,[eax]` vs
  `nop`) is the size gap of a register choice above it (com_wait_byte 0x2df70). [generic]
- **Rows that are only stack displacements = the slot-permutation dead end** (below): count them
  first (showlines 0x2a268: 10 of 18). Count them, and every other class, with one pass over the two
  full listings: normalise each instruction (addresses as `@`) and subtract the COUNTERS per side -
  what remains is the register/width/epilogue gap, not the slot permutation. [generic]
- **Equal instruction counts but a byte compare that starts on a branch displacement = a register
  home, not a missing instruction.** AL/EAX own the short absolute forms (`a0`-`a3`, 5 bytes; any
  other register 6, `add dl,imm8` 3 vs 2), so one home shifts every later jump (shots_move 460 vs
  462 bytes at 0x2613b). Sum instruction sizes per side to find the store. [generic]
- **Line up the two listings' loads before probing:** load ORDER differs = 9.5b's scheduler, only a
  statement reordering can move it (net_input, fn_16ca0 fixed that way; fn_19db8's switch
  controlling expression is not a statement: SKIP). Order equal, homes permuted = the colourer, try
  the levers below, then SKIP. A whole function whose temporaries mirror the game's EAX<->EDX is
  that class: four C spellings of race_run's `x * 8` pointer walk (0x3066d, four sites) all scored
  equal to the control. [generic]
- **A `cmp` row whose only difference is the operand order is a dead end, not a lever:** 9.5b gives a
  comparison's source-LEFT operand both the cmp destination and the first register home, so flipping
  the C operands swaps the two temps and leaves the emitted `cmp` alone (car_laps 0x12ee0, 21
  spellings; intro 0x351ae, `j == k + 1` 12 -> 14). [generic]
- **Read the width of a compare AND of a load:** `cmp r16,r16` = both shorts in C, `cmp r32,r32` =
  at least one int; `movzx ax,al` vs the game's `movzx eax,al`, or `cmp dword [slot],N` vs its
  `cmp word`, is the same signal. Pair the game-only and ours-only rows that agree on opcode and
  immediate but differ in width: menu 0x155c0 `isnet` int 267 -> 233, cars_menu 0x1a380 `cd15c`/
  `slide_up`/`slide_dn` short 499 -> 476. Fix the C types before logging a register pairing
  (fn_16eac: `int cnt` 17 -> 11 rows). A
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

- **A data address of ours can be plain wrong and no row count sees it** (iter 59, fn_173c4: a table
  written at 0x7b230 instead of 0x7b238, two record fans reading faces+0x44/+0x60 instead of +0x3c/
  +0x58 - 3 bugs, fndiff rows and the MATCH count unmoved). Our disp32 data operands are linker
  fixups, so fndiff masks them, and nmrefs labels an indexed access by the array it lands in
  (`array_of`), which folds a base wrong by a few bytes into that array's name. The OMF addend is in
  the .OBJ bytes at the fixup location (libmatch's `fixups_of` reports 0 for loc 9), so a resolved
  address = the symbol's address in `.work/link/symmap.txt` + those bytes. `tools/wc/globrefs.py
  ww .work/src/WW.OBJ NAME 0xADDR` prints the data addresses only one side touches plus count gaps;
  run it on every near miss that touches a table (`counts 0x7d868 ours x1 game x2` = the game's dead
  load, a typical SAME line - read the game's instruction before believing one).
- **`ours only ?signal_` + `game only 0x43231` is ONE clib call, not a behaviour difference** (iter
  61: enter_name 0x1cb44, chat 0x14d00 and main 0x035ca4 each carried such a pair through several
  passes). In the unlinked `.OBJ` a library call is an *external* (`signal_`, `getenv_`, `int386x_`)
  and nmrefs can name it only if `analysis/ww_names.txt` has a row; unnamed, it prints one-sided on
  both sides. `.work/link/symmap.txt` proves the binding without guessing: its `observed` rows are
  the target found in the *game's* bytes at the site of our fixup (`signal_ 0x043231 observed`).
  Name the entry and the pair cancels - code shape confirms the name (0x43231 range-checks 1..0xc =
  a signal number and installs 0x431fa; 0x4338f walks the environ block at [0x644ec]; 0x43065 is the
  `int386x` register shuffle, `mov ebx,ecx / esi,eax / edi,edx` then `call 0x4c748`). After naming
  all three, enter_name and main showed no one-sided address and no one-sided jump block at all.
  Read a `?name_` line before believing any DIFFER next to a library call.
- **globrefs prints every string of the game as `game only`** (iter 61, chat): it resolves only
  *external* fixups through symmap, and our own literals hang off *segment* fixups, which it skips.
  nmrefs does resolve them (it votes a common base), so strings from nmrefs, data addresses from
  globrefs. [generic]
- **A census tool that finds a function by `startswith` compares the wrong body, and a our-side scan
  that ignores absolute `disp32`s invents one-sided addresses** (iter 65, `tools/wc/globrefs.py`, a
  census of all 102 near misses): `nm.startswith(name)` resolved `fade_out` to the 20-byte
  `fade_out_screen_` and `main` to `main_menu_names_` (Watcom OMF publics carry a trailing underscore)
  - match `name` or `name + "_"` and print the public it picked; the last public in `_TEXT` has no
  following public, so the range end must default to the segment length or the tool dies in
  `StopIteration`, and a sweep that pipes each run through `sed -n '2,20p'` drops the traceback line,
  so that function silently contributes nothing. It also collected only ext fixups and immediates on
  our side while taking every memory displacement on the game's, so the game's
  `mov [edx+0x6e6e0],eax` 0x17a3a, `mov [edx+0x6e6e4],eax` 0x17a45 and `mov edx,[eax+0x6508e]`
  0x1828c printed as `game only` against our byte-identical operands (0x79ae, 0x79b9, 0x81f0: an
  absolute `disp32` with no fixup on it). Fix: take our MEM displacements too unless a fixup covers
  the four bytes at `ins.address + ins.disp_offset` (capstone 5 has `disp_offset`). After the three
  fixes, one near miss of 102 still had a one-sided non-string address - showlines 0x2a268, the
  relocated `&netstats[1]` addend (SAME). `tools/wc/test_globrefs.py` pins all three fixes. Fed to
  DREA the same session (harness/TRAPS.md "Static analysis tools"). [generic]
- **A `check.sh` MATCH means the bytes agree, not the operands - 4556 of them are masked inside the
  202 MATCHed functions** (iter 66, `tools/wc/fixsites.py`). objmatch checks only the *named*
  externals site by site (`WRONG sym@addr`, 3116 agree) plus one base per segment, so an internal
  `call`, a switch-table dword, an unnamed global and a string literal inside a MATCHed function are
  masked and unchecked - the very class the hybrid link's two bugs came from. Inside a MATCHed
  function the mirror address is exact (the unmasked bytes are equal), so compare site by site:
  `ext` = its symmap address + the fixup disp + the addend in our bytes; `seg _TEXT` = the target
  offset held in our bytes (the fixup disp is 0), mapped through the public containing it to that
  function's game address + intra-function offset; a `_TEXT` target below the first public is a string
  literal WCC put in the code segment (our `_TEXT+0/0x144/0x1ec/0x228` are the game's
  0x10010/0x10154/0x101fc/0x10238, "UNO", "JAN FEB MAR...", "MAINMENU", "SHAUN"); every other segment
  = one base by vote. Result 0 disagreements (ext 4133, `seg _TEXT` 94, CONST 327, `_BSS` 2): no
  MATCHed function reads another global, calls another function or passes another literal. Two traps
  on the way: a `.OBJ` data segment is a *list of offset/data records* (51 for CONST), so "its bytes
  at offset X" from the concatenated records is not what the link puts there - that version called 325
  of 327 literals different while the same 327 agree on base 0x60004; and a switch table whose targets
  lie in a near miss is not decidable at all (54 entries in 6 tables - kart_frame, fn_19db8,
  cars_menu, kart_fire, main_menu, trial_book), because a near miss's blocks are laid out differently
  and its intra-function offsets mean nothing (QUESTIONS 22). `tools/wc/test_fixsites.py` pins both.
  Its `--arms` pins are frame-slot sensitive: the C fix of iter 75 (`p7d838`'s bump, once per pass) moved
  which locals `cars_menu` keeps in registers, so the game's `[esp+0x16c]` pair is now ours at
  `[esp+0x144]` and our arm stores the 6 as an immediate where iter 74's went through DI - re-read the
  pins from the tool's output after a C change, never hand-copy them.
  Fed to DREA the same session (harness/TRAPS.md "Old compilers", commit e28a7eb). [generic]
- **The switch-table entries that census leaves over ARE decidable - compare the two arm blocks, not the
  entry addresses** (iter 67, `tools/wc/fixsites.py ww .work/src/WW.OBJ --arms NAME [-v]`). A pair is linked
  by its slot: both sides' table sits at the same mirrored offsets of a MATCHed function, so slot *i* of ours
  is slot *i* of the game's and the question is what the two blocks DO. Per block (up to its first branch or
  `ret`): mnemonic + `op_str` with the numbers masked (the game folds a base into a displacement -
  `[eax+0x6aaa2]` is our `[eax+0x12]` + fixup) followed by the resolved fields: 4-byte disps/immediates
  through the same fixup resolution the census uses, **every 1- and 2-byte immediate too** (`mov word
  [ents frame], 6` hides its 6 in a 2-byte field, and a wrong one there is exactly this bug class), jump
  targets masked and any field pointing into a near-miss body masked. `sorted(a) == sorted(b)` says "same
  instructions, another order" - then hand-check that the read-before-write order of each address the block
  touches held. Of the 36 entries left from iter 66, that found ONE bug (`cars_menu`'s carstat==0 slide arm
  never stored `ents[i].frame = 2`, game 0x1b7de: the arm reads `ents` either way so no address census saw
  it, and fndiff rows went 433 -> 431 when the store came back) and 35 same-shape (another register; the
  value carried in ECX to its store, 0x2459c -> 0x245bb; `mov ecx,[curpos]` 0x24613 hoisted above a branch;
  independent stores swapped). Capstone needs the game's virtual address, not the file offset, or every
  branch/call target it prints is off by the 0x10000 code base - that faked 3 "different call targets".
  Fed to DREA the same session (harness/TRAPS.md "Old compilers"). [generic]
- **Our C can hold the game's expression and never use it, and then both counters move the WRONG way**
  (iter 60, cars_menu: `capx = (0x140 - 8*strlen(buf1))/2` was in the source but the print call passed
  `x2e`, so 9.5b deleted the division and the caption drew at x=46 instead of centred; the same
  function was also missing one of the game's three `drawsprite` calls for the selection box, 0x1ae5d).
  Both made our function shorter and its fndiff count SMALLER (476 -> 433 while getting two things
  right), so a shrinking row count is no evidence of a fix. nmrefs named them: `game more 1x
  0x36fd0(drawsprite)` and `game only #0x140 #0x1f` (`mov eax,0x140` + `sar edx,0x1f` 0x1a456/0x1a466
  = the game's divide by the 320 px screen width). Read a length gap as missing calls, not padding.
  Fed to DREA the same session (harness/TRAPS.md, iter 60 - the wacky commit for it is the DREA commit
  "Traps: the port's own dead code deletes a computation the original performs"). [generic]

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

- **Frame size first: our `sub esp, N` differs from the game's = a local array has the wrong size.**
  Read the array sizes off the game's slots: overlay_draw's frame 0x34 with buf at [esp+0x14] and
  dword slots from 0x20 meant `char buf[0xc]`, not 0x14 (62 -> 46, every scalar slot moved back);
  race_run's word array at [esp+0x28] with two slots from 0x3c meant `short placed[10]` although its
  largest index is 7 (0x2fd8c, 128 -> 120). Also scores_screen `long sc[10]`, time_str `buf[4]`. [generic]
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
  A local the game only tests and divides with is `short` as well: `cmp ax,word [esp+4]` 0x267db and
  `movsx ebx,di` + `idiv ebx` 0x26803, where an int gives `cmp r32` + `idiv dword [esp]`
  (lights_project `short sy`, `short a`: 112 -> 85). [generic]
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
  global changes other functions. It pins stores, not loads: an `extern char *volatile` alias on the
  other side of a swapped pair of loads left the order alone (shot_step 0x25f46). [generic]
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
  plain `for` (crack_draw); a `while (i <= g)` keeps 9.5b's TOP test where the game has the test at
  the bottom re-reading the global - write `for (i = 1, x = 4; i <= g; i++)` (fn_173c4 0x175d9). [generic]
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
- **Check a permute.py winner's diff before believing its score:** until iter 56 two mutations broke
  the code silently - `comm` swapped `1 + X` in `- 1 + X` (a sign flip) and `bound` rewrote the
  `> 1)` of `>> 1)` into `>>= 2)` (a store into the field). kart_bump's "49 -> 42" came only from
  those two; the four safe mutations of that winner score 49. Both regexes are fixed; results logged
  before iter 56 that list `comm` or `bound` were scored on wrong code. [generic]
- **Score the control copy before trusting a variants.py probe:** copies of a function appended to
  the end of a probe file do not always compile like the original in place. race_records_menu
  0x1d13c: the unchanged copy scored 38 rows / 476 bytes appended, 12 / 512 in place (fn_1c0f4's
  control did reproduce). When the control differs, compile each variant in place in its own
  directory (parallel wcc.sh runs are fine). [generic]
- **A near miss can hide a behaviour bug the row count never shows:** a wrong field (cboxes[1].x2
  for .x), a swapped string, a store into the wrong table, or code made unreachable (no label after
  a `goto`, which 9.5b drops without a warning: fn_1832c lost its joystick path and trial_book
  call). `tools/wc/nmrefs.py` compares what both sides reference; run it after every change. One
  shape to watch: the game computes an expression over globals once and reuses that register on both
  sides of a store to one of them (`sub [spr_w],ax` then `mov [clipy],eax`, 0x34346/0x3435f) - the
  source had a local; writing the expression twice re-reads the stored value and changes behaviour
  (players_ahead's clipy was always 0, findings/video.md). [generic]
- **Read a nmrefs count line as a store census first:** count that global's reads and writes on both
  sides (decoded operands with an absolute address; the writes are the `mov`/`add`-style ones in the
  destination). Equal write counts and a game-side `add [g],imm` (0x264f8 `add [p7d838],4`), a frame
  slot (0x25694 `mov [esp],edx` for `sy`) or a folded base (0x2134f `movsx edx,[ebp+0x7299c]` =
  `attrs[i].w`) mean shape, not behaviour; iter 58's three passes found only that. What is real: a
  store that is missing, and a global left with a different value at the return - xref the global and
  check who reads it without writing (shots_project's v7d874: the game stores the past-the-table
  pointer 0x26697, and it is dead because every reader sits under a writer, findings/game-loop.md).
  Two shapes that end the hunt: C's `g += x` is ONE address-bearing instruction (`add [p7d838],eax`
  0x2692e) where a rebuild that keeps the value in a register has load+store (lights_project 0x266a8,
  ours x5 vs game x4), and the game skips a flag test Watcom can prove from an earlier `je`
  (race_results 0x2bff0: three source ifs, two reads, the scores_screen arm tests only its other operand
  0x2d218, with a dead `je` 0x2d1f7 left behind) - deleting your own test to match the count changes
  behaviour. Pair the sites in instruction order and compare where each failing test jumps. [generic]
- **A one-sided nmrefs jump-target block is three SAME classes before it is a bug:** (1) the game
  tail-merges a store pair, so the block an arm jumps into uses only the second half (shot_step
  0x25ff1 stores x then `jmp 0x26021` 0x26008 into the shared y store, where our `return` jumps to
  the epilogue and our tail block re-reads the struct); (2) the game skips a block it can prove dead
  from the condition that got it there (ents_show 0x145c3 `jmp 0x146bd` = the loop tail, leaving both
  `sel == -1 && ...` blocks 0x14623/0x1468d, reached only from inside `if (sel != -1)`); (3) the game
  keeps ONE head for nested retry loops, so its retry jumps re-test an outer condition our build
  tests once (fn_1be3c 0x1beae: the rand retries 0x1bec2/0x1bec8 and the duplicate-found `je 0x1beae`
  all land on the `splitscreen` test, ours land on the call 0x1beb8 or on the bottom test 0x1bef5).
  In all three compare reachability and the values at the stores, not the jump targets. [generic]
- **An unsigned `cmp eax,1; jbe A` + `cmp eax,2; je B` + `jmp` chain is a `switch` with `case 0: case
  1:`**, not `if (v <= 1) ... else if (v == 2)` (signed `jg`, other block order). A block the game
  jumps BACK into from a later test is the same statements written twice in the source and
  cross-jumped: `if (netgame == 0) { A } else switch (v) { case 0: case 1: A; break; case 2: B; }`
  (fn_1c0f4 0x1c538-0x1c58b, 67 -> 14). [generic]
- **A row loop over `char rows[N][K]` from 1: an explicit `char *r` declared last and stepped in the
  for header** (`for (i = 1, r = rows[1]; i <= n; i++, r += K)`) took volume_menu 0x1e9cc 64 -> 48
  and fixed the scalar slots after it; declared first or set in the body, no gain. [generic]
- **Two local arrays in each other's slots: a pointer to the one the game puts at [esp], initialised
  at entry.** fn_17928's `char *nb = numbuf;` (declared last, used only in the itoa call) moved
  numbuf to [esp] and msg to [esp+0x50] like the game: 12 `lea edi,[esp+0x50]` rows gone for one
  `mov ecx,esp`. Declaration order, `(void)numbuf`, a block-local array or a late `nb = numbuf` did
  nothing; using nb everywhere cost an EBP frame. [generic]
- **Stores near an inline strcpy slide into its setup unit**; which ones is a source-position fact,
  probe every position (classes_menu 0x19f01-0x19f27).
- **A permuted group of scalar slots: one `volatile` array of those values** (wtext 0x146d4, 6 ->
  MATCH; a plain `int sl[4]` or struct with constant indices is scalarized and its slots scramble,
  6 -> 21). Only works when they are the function's ONLY spilled locals - with other scalar slots
  around, the elements are still coloured individually (screenshot 0x114c8, release_check 0x034458:
  no gain). [generic] [generic]

### Dead ends: SKIP at once (0 variants) unless you bring a new idea

- **Stack-slot permutation of scalar locals** (QUESTIONS 9, 10), unless one of the swapped slots
  is a compiler-made pointer (lever above) or the group is written as one `volatile` array (Levers,
  wtext 0x146d4): neither declaration nor first-use
  order (arrays DO follow declaration order, ascending from esp+0). Exceptions, iter 56: release_check 0x34458
  went 7 -> MATCH with the loop step written `i = 1 + i` instead of `i++`; in kart_bump (11 shorts,
  8 spilled) a random declaration order from permute.py's `declorder` mutation took 49 -> 36 rows -
  it moved register homes as much as slots. Hand-picked orders do not find this; let the permuter
  shuffle the declarations. 43 orders in enter_name, also
  release_check, wtext, scores_screen, fn_1c0f4, showlines, cars_menu. In a function that already
  has 20+ scalar slots the pointer lever backfires - it renumbers every slot (trial_book 0x2e4b8,
  four spellings: probe 155 -> 256). Size a local array from the
  frame, not its largest index (scores_screen `long sc[10]`); never-used locals are dropped, so a
  frame gap means the game used those slots - but a gap BETWEEN two arrays is the earlier array being
  bigger than the code writes (fn_193a8: `char name[0x28]`, 12 bytes of a 40-byte buffer used, frame
  0x310 -> the game's 0x32c). The reverse too: an over-sized buffer inflates the frame
  and every slot displacement (`char buf[8]` gave time_str 0x2a61d a 0x14 frame where the game has
  0x10; `buf[4]` fixed it, 33 -> 31 rows). [generic]
- **A global kept in a register across a loop** (load at entry, store before each call and at exit)
  is NOT a dead end: the game's source used the global itself as the working variable. Drop the
  local copy and compute in the global (`spr_x = p->x - 0x400; if (spr_x < 0) spr_x = 0; ...`):
  minimap 0x2a4ac 44 -> MATCH, fn_16eac (`n = nplayers` copy removed) 11 -> MATCH. Signature to
  scan for: `mov [G],r ... call; mov r,[G]` in the game - the store can sit up to a dozen
  instructions before the call (shot_fire 0x2d5ad: v7d874 walks the shot table, 57 -> MATCH;
  fn_16d68: `v7d7f8 = &trklines[k]`, stored once at the exit, 58 -> 26). Mirror image: if the game ALSO
  reloads the global after its calls (`mov r,[G]` 0x26403/0x26491/0x2667e), the source used a local
  pointer plus `G = p;` in the body - shots_project 0x2632c 155 -> 117 that way (open in
  players_project/ents_project). A global that is already
  the working variable and still is not cached (players_ahead's v7d800) is unexplained. The lever
  also fails when the cached global is a struct pointer used hundreds of times: kart_frame 0x290a0
  with `k = curkart` at entry, `k->` at all 139 sites and `curkart = k; k = curkart;` around every
  call and return scored 535 -> 533, and the game still had 19 more 0x7d5b0 references. It works for
  a global only READ, and 9.5b does the promoting itself: fn_173c4's `short mp` local (myplayer) had
  to go to get the load-at-entry/reload-after-call/store-at-exit trio (0x173ca/0x17405/0x17855) - a
  local also lets 9.5b CSE every derived `index * stride` address out of the loops, which is how it
  hid 3 array references and 6 jump blocks from nmrefs there (0x173c4, 189 -> 100). [generic]
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
  reloaded after the call - fn_1d704 0x1d7b5, 11 spellings). `tools/wc/permute.py` does not move
  these pairs either (shot_step 0x25f46, track_setup 0x124f6: 270 copies over 9 rounds, best =
  control every round). `-3` vs `-3r` is not the cause
  (identical object). [generic]
- **Not a dead end: `mov r,N*stride; add r,sym` is an INDEX loop.** 9.5b folds a pointer's
  `&netstats[1]` (or `netstats + 1`, `(char *)netstats + 0x3c`) into one immediate; only
  `for (i = 1; ...; i++) f(netstats[i])` makes the strength-reduced pointer start as
  `mov r,0x3c; add r,netstats` (iter 73 probe, 8 spellings; showlines 18 -> 13 rows and the game's
  size). Also: a base one row earlier names an array base - declare it (`mrows` 0x7bc00,
  trial_records_menu). [generic]
- **Store->load value forwarding:** `v->f0a = d; x = v->f0a;` compiles to a register copy before
  the store, no reload; a `(volatile short *)` cast on the read does not force one (9.5b forwards
  anyway). The game's `mov word [ebx+0xa],dx; mov cx,[ebx+0xa]` (kart_catchup 0x28bf8) is not
  reachable; a partial store breaks the form. SKIP. [generic]
- **Extra bytes from a larger frame for one more local** cost spills and re-colour unrelated temps
  far away (kart_draw 17 -> 28; track_load's v88b88 load): score the whole function. [generic]
- **The game's single `ret` is not a target to reach:** merging our returns into one tail (`goto`, one
  `return`) so the epilogue count matches costs rows - menu 0x155c0 has one epilogue at 0x163df and our
  five duplicates align with its per-site `jmp` tail better merged (233 -> 261). The game's C may well
  have had one exit; the row count does not care. [generic]
- **There is no per-function optimisation control in WCC386:** `#pragma loopopt` is a WPP pragma
  and the pipeline (wcc386 on .C, no preprocessor) silently ignores it - `#pragma loopopt OFF`
  on fn_16f90 changed nothing (0x16f90, the loop-strength-reduction dead-end class). [generic]

### General rebuild facts

- **9.5b frame and evaluation-order rules, measured (iter 73 probes, Open Watcom cg source as the
  map: `temps.c` AssignOtherLocals, `x86temps.c` TempAllocBefore, `dataflo.c` RoughSortTemps):**
  (a) address-taken locals and arrays are sorted by SIZE, smallest highest in the frame (char, short,
  int, arrays), ties in declaration order with the first declared highest; first use does not
  matter. So `short` vs `int` moves a local across the whole frame. (b) Spilled scalars in a clean
  probe (10 ints live across calls, straight-line or one loop): the most-used get registers in the
  order EDX, EBX, ECX, ESI, EDI/EBP, the spilled ones sit by use count, more uses = higher slot, and
  declaration order, assignment order and the names change nothing. A use inside a loop outweighs
  16 uses outside it. BUT real functions break (b): credits_screen's `dir` (6 references, inner-loop
  ones) sits below `first`/`y` (3 each) on both sides. The mechanism (OW `generate.c` Generate:
  MakeConflicts -> SplitVars -> MakeConflicts -> RegAlloc -> AssignTemps): slots go out top-down in
  the temp list's order after a size sort, and that order is the savings sort (`RoughSortTemps`,
  weighted IR reference counts, unstable ShellSort) of the LAST MakeConflicts plus temps created
  after it (SplitVars gives every later disconnected live range of a reused variable a new temp).
  IR counts are not C counts, and ties are reordered by the ShellSort's gaps, so the result is
  chaotic: in a credits_screen probe one extra read of `first` moved `page` to the top slot, extra
  reads of `y` moved `first` to the bottom (iter 73, 6 probes). Declaration order acts only through
  ties - which is why a shuffle (`declorder`) sometimes wins and hand-picked orders do not. Not
  solvable by rules; it needs a simulation of the compiler, or search. (c) Of two operands the LARGER subtree is
  evaluated first whatever the written order (`g(a) + g(b) * c` == `g(b) * c + g(a)`); written
  order decides only between equal subtrees, and no-op wrappers (`(int)`, `+x`, `| 0`, `-(-x)`) are
  folded, so they cannot steer it: a game that evaluates the smaller side first had a different
  tree (grouping, or a temp in its own statement). permute.py's `comm` can only help on equal
  subtrees. [generic]
- **Compare the data, not only the code, and expect dead bytes**: `tools/wc/datamatch.py ww
  .work/src/WW.OBJ` (iter 68) puts our build's data groups on the game's image. Two traps, both found
  getting a clean run: (a) the group's base must be voted over **every** occurrence of each anchor -
  counting only the first hit of each anchor put the CONST group 4 bytes early and made 8225 of its 9046
  bytes look wrong; (b) a differing byte is behaviour only if a dword somewhere in the image holds
  its address, because the linker shares pooled literals and the game's pool keeps dead text where our
  build has NUL padding (findings/executables.md "Data layout (obj2)"). `tools/wc/test_datamatch.py`
  pins both, with a flipped literal byte as the mutation. [generic]
- **`src/check.sh` after a compile error scores the PREVIOUS object** unless it deletes it first
  (fixed iter 36: `rm -f .work/src/WW.OBJ`); grep its output for `Error!` before reading a count.
  [generic]
- **`tools/le/xref.py` already prints the callers** of an address: capstone shows a `call rel32`
  with its resolved absolute target, so the operand scan catches it (`xref.py ww 0x357ac` ->
  `call 0x357ac` at 0x1691d). Writing an `0xE8` sweep script is redundant; `.work` holds four.
  [generic]
- **A block's fields need a range census, not an xref of its base**: Watcom reads a struct/array
  element through its own absolute address (`cfg7ee1c + 0x3e` is `word [0x7ee5a]` = `v7ee5a`), so
  `xref.py ww 0x7ee1c` returns only the whole-block transfers; `tools/le/rangexref.py ww 0x7ee1c
  0x7f2ae` -> 17 addresses, which is what proves the `WACKY.ING` record area is unread. The same trap
  in the listing: when the base is held in a register capstone prints the field reads with the base
  folded into the displacement (`movsx edx, word ptr [ebp + 0x7299c]` = `sprattr[0].w`), so grepping
  the disassembly for the name finds only the 9 base loads - census the fields by offset
  (`grep -oE '\[[a-z0-9]+ \+ 0x729..\]'` gave the whole SPRITE.ATR read set, 0x72998/9c/9e/a4/a6/bc).
  [generic]
- Structs are pack(1) (`imul 0x6a` where the game has `0x68` = natural packing). [generic]
- Literals land at the game's CONST addresses only when functions are added in address order.
- **A function's entry block can land far from its body.** When function A ends in a tail call to
  B and B is not compiled yet, 9.5b emits B's entry block right after A (the `jmp` to the next
  instruction is dropped) and B's remaining blocks where B is defined. fade_out_screen 0x1bf80 falls
  into fade_out's head at 0x1bf94; fade_out is defined after pic_show, so its loop tail sits at
  0x32cc1 straight after pic_show's `ret` (iter 56c: a "gap" plus a "padding diff" were one
  function). Signs: a function with no prologue that falls into the next, and a block right after
  another function's `ret` with no alignment and no entry of its own. [generic]
- A caller placed after a small function's definition gets it inlined by -oe: when the game calls
  it, call it through a `#pragma aux` alias (fade_out_screen_call in main_menu; fn_1c068 is
  left undefined for the same reason).
- A switch's jump table sits in the gap BEFORE the function (0x1a360 in cars_menu), table order is
  source-label order, and case bodies are emitted in source order: sort the cases by jump-table
  target before writing the switch (main_menu case 7 before case 6; 0x19da8 for fn_19db8).
  [generic]
- A Watcom switch table may be indexed by the switch value **minus one**: `mov bx,[obj+0x18]` +
  `dec ebx` + `cmp bx,0xd; ja default` (kart_frame 0x298cf, table 0x29068 - entry 0 is `case 1`,
  entry 13 is `case 14`). Read the index computation before mapping entries to cases: otherwise a
  nmrefs gap like `game more 4x <table>` / `ours more 1x <other table>` reads as the wrong table in
  an arm, when it is plain block merging (kart_frame's 9 ANIM_RESET base loads vs our 5). [generic]
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
- The gdb stub refuses every breakpoint above linear 0xFFFFF (`Z0` -> `E01`, dos-re-suite's
  `gdb_addr_to_seg_off`): a DOS/4GW program's code (here 0x1e2000+) cannot be broken on at all.
  Halt + read registers works: sample EIP/SS in a loop instead (SS tells the program from an ISR
  stack). [generic]
- After `load` of a saved state, QMP key taps did not reach the game (it stayed in `menu_`): for a
  key-driven run boot fresh and press the keys. A sampler that halts every 30 ms also swallows taps:
  press first, then sample. [generic]
- WLINK 9.5b page-faults (`CS:EIP = 000Ch:0001FFD5h`, stock DOSBox then aborts) on a hand-written OMF
  object that has fixups but lacks WCC386's two COMENT records (class 0xAA `OS220`, 0x9B `3fOp`);
  OMF segment indices are 1-based (index 0 crashes it too). With `option quiet` nothing is printed:
  read the map. [generic]
- A near miss with a low-looking row count can still be semantically wrong: fn_1832c read 0x300
  bytes into a 20-byte stack array; only running the hybrid link (tools/link) showed it.
- The LE entry is obj1 offset 201128 *decimal* = 0x311a8 -> 0x411a8, not 0x41128.
- A game global written as a number in C (`strcpy((char *)0x80ace, ...)`) matches byte for byte,
  because the game's unrelocated bytes hold the same constant, but it has no fixup: the linked program
  writes to linear 0x80ace, not to the relocated variable (DOS/4GW loads the data object elsewhere).
  It hung the hybrid's race load (fn_17928, music ISR starved). Use an extern symbol (`litNNNNN`,
  src/ww.c line 18). [generic]
- `drawsprite` 0x36fd0 / `f373ee` 0x373ee are column-major: `spr_w` = bytes per column = the height,
  `spr_h` = the number of columns = the width, cell byte = col*spr_w+row (findings/video.md Traps).
- Wrong screen block but the reader, the filler and the index writers are all byte-identical? Bisect
  the *state*: `ORIG=<fn> tools/link/racetest.sh` builds that C function from the game's bytes; census
  the block's pixels before and after. Proving which bytes were read (decode the data file) beats
  reading more code. `[generic]` (iter 72)
- `tools/le/disasm.py` stops at the first undecodable byte, so a sweep of the whole code object is
  silently truncated and a grep for "this address is never stored" may only have seen the first part.
  Use `tools/le/gd.py <prog> <from> <to>` (same annotation, skips a bad byte and resumes). `[generic]`

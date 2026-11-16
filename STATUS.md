# STATUS

Short: where the project stands, and what is next. History is in git; do not
keep a session diary here.

## State

- Executables identified: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, options `-s -3r -oailrt`
  (`-oneatx` used; findings/executables.md). Code layout: one C module, an asm block, libraries incl. the
  Apogee Sound System.
- Toolchain: Watcom 9.5, 9.5a, 9.5b installed in `.work/toolchains/inst` (not committed).
- Approach: matching rebuild (../DREA METHOD.md section 9), then a translated port (section 10).
- Harness: not started. Port: not started.

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x19da6, 82 functions
   byte-identical and 30 near misses (`src/check.sh`; near misses are marked `differs:` in the source, and the
   register/scheduling ones are QUESTIONS.md 2). Layout notes: struct player pack(1), struct face 0x98,
   calling convention arg1=EAX arg2=EDX arg3=EBX arg4=ECX (stack args pushed by the caller and popped by
   the callee, `ret 4` - see findings/menus.md Traps). `src/ww.c` is ~5.1 K lines: grep, don't read whole.
   `tools/wc/starts.py ww LO HI` lists function starts (it can also list loop bodies that follow a jmp).
   Watcom 9.5 `-oe` inlines small user functions (later-defined included); the game's calls to
   seekdat/telldat/datsize/readdat stay unresolved (GOTCHAS). Functions after the data-file block
   (0x14128-0x14260) go at the end of the file.
   Next in address order: **fn_19db8** (0x19db8, a switch: `jmp dword ptr cs:[eax*4+0x19da8]` on
   `v88bb2 - 1`, the cases write 0x7ece0 and `[0x6aa40]`), then 0x19eac and 0x1a148 (lap menu).
2. ctl_label 0x19c68 is a 2-byte near miss: the game zero-extends a `== 0` boolean into EAX at
   0x19d0e, our 9.5b uses EBX plus `mov ebx,eax` (GOTCHAS, QUESTIONS.md 2e). Same rule as the crc
   question 1: try to find what flips the choice (a second boolean live in the block? an
   if-converted constant?), otherwise leave it `differs:`.
3. Probe the `v7ebc0` register promotion in a small T-file (GOTCHAS "Watcom promotes a global read
   inside a loop"): find what makes 9.5b do it in wait_or_key and fn_193a8's build but not in our
   fn_193a8. If it is pinned, fn_193a8 and maybe fn_1832c shrink by ~90 bytes each.
4. Fix the two known call-shape traps before the menus are retried: the `f375cd` aux `parm [edx]`
   forces mov+movzwl where the game does a 16-bit partial load (0x19506), and
   `telldat() + datsize() - 0x300` accumulates in the wrong register (0x194c8).
5. Inventory: archives and file formats (WACKY.DAT), strings. Find what dat record "BSBCS.PCX" /
   "(B)SBn.PCX" is (findings/menus.md).
6. Get the original running under the harness; record the boot's segments or
   LE deltas and a first save state in `captures/`.
7. Copy this session's three generic Watcom traps (GOTCHAS: EBX boolean in a jump-target block,
   `push ebx` as a register-home signal, `strcpy(p,"")` = one byte from the string pool) into
   `../DREA/reference/DOS.md` - the overnight policy blocked that write.

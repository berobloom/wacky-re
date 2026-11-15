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

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1790D (through fn_17888), 78 functions
   byte-identical and ~20 near misses (`src/check.sh`; near misses are marked `differs:` in the source, and the
   register/scheduling ones are QUESTIONS.md 2). Session 2026-08-05b added fn_16f90/fn_173c4/fn_17888,
   struct player pack(1) layout, struct face 0x98 record, calling convention arg1=EAX arg2=EDX arg3=EBX arg4=ECX.
   Next in address order: fn_17928 (2.4 KB setup monster, 0x17928-0x182cd, one frame, multiple internal rets),
   dead block 0x182ce-0x18326 (no xrefs, still needs bytes), fn_1832c (~4 KB), then 0x19c68 / 0x19db8 / 0x19eac /
   0x1a148 (lap menu, tractable). `src/ww.c` is ~4 K lines: grep, don't read whole. `tools/wc/starts.py ww LO HI` lists function starts (it can
   also list loop bodies that follow a jmp). Watcom 9.5 `-oe` inlines functions defined later in the
   file too: readdat/datsize/seekdat/since2/sfx_play2/view2_* calls show up inline. Functions after the
   data-file block (0x14128-0x14260) go at the end of the file.
2. Inventory: archives and file formats (WACKY.DAT), strings.
2. Get the original running under the harness; record the boot's segments or
   LE deltas and a first save state in `captures/`.
3. Function list and names (`tools/le/funcs.py` or a Ghidra dump) for the
   main program.

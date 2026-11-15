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

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1437C, 62 functions byte-identical and 15 near
   misses (`src/check.sh`; near misses are marked `differs:` in the source, and the stack-slot ones are QUESTIONS.md 2).
   Next: continue at 0x14380 in address order. `tools/wc/starts.py ww LO HI` lists function starts (it can
   also list loop bodies that follow a jmp). Watcom 9.5 `-oe` inlines functions defined later in the
   file too: readdat/datsize/seekdat/since2/sfx_play2/view2_* calls show up inline. Functions after the
   data-file block (0x14128-0x14260) go at the end of the file.
2. Inventory: archives and file formats (WACKY.DAT), strings.
2. Get the original running under the harness; record the boot's segments or
   LE deltas and a first save state in `captures/`.
3. Function list and names (`tools/le/funcs.py` or a Ghidra dump) for the
   main program.

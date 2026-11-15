# STATUS

Short: where the project stands, and what is next. History is in git; do not
keep a session diary here.

## State

- Executables identified: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, options `-s -3r -oilrt`
  (10 of 11 probe functions byte-identical, `analysis/optpin/check.sh`; findings/executables.md).
- Toolchain: Watcom 9.5, 9.5a, 9.5b installed in `.work/toolchains/inst` (not committed).
- Approach: matching rebuild (../DREA METHOD.md section 9), then a translated port (section 10).
- Harness: not started. Port: not started.

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Matching rebuild, set up: the layout is known (findings/executables.md, Code layout: one C module
   0x10010-0x36A4D, asm 0x36A4E-0x409EE, then libraries). Next: a per-function check that also compares
   fixup targets (objmatch masks them), then decompile in address order; the asm as a byte-exact source.
   Remaining options and QUESTIONS.md 1 as they come up.
2. Inventory: archives and file formats (WACKY.DAT), strings.
2. Get the original running under the harness; record the boot's segments or
   LE deltas and a first save state in `captures/`.
3. Function list and names (`tools/le/funcs.py` or a Ghidra dump) for the
   main program.

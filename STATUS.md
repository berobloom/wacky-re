# STATUS

Short: where the project stands, and what is next. History is in git; do not
keep a session diary here.

## State

- Executables identified: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b** (findings/executables.md).
- Toolchain: Watcom 9.5, 9.5a, 9.5b installed in `.work/toolchains/inst` (not committed).
- Approach: matching rebuild (../DREA METHOD.md section 9), then a translated port (section 10).
- Harness: not started. Port: not started.

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Pin the compiler options against a few small game routines (findings/executables.md, Open).
2. Inventory: archives and file formats (WACKY.DAT), strings.
2. Get the original running under the harness; record the boot's segments or
   LE deltas and a first save state in `captures/`.
3. Function list and names (`tools/le/funcs.py` or a Ghidra dump) for the
   main program.

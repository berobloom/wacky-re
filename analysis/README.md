# analysis/

- `<prog>_names.txt`: one line per named address, read by `tools/le/disasm.py`
  and `tools/ghidra/decomp.sh`:

  ```
  0x15a14 winner_film          # CONFIRMED (strings): plays w*.smk for the leader
  ```

  Address in the convention AGENTS.md sets (LE image address, or CS offset for
  real mode). Name, then `# LEVEL: why`.
- `<prog>_funcs.tsv`: `tools/le/funcs.py <prog>` output (address, calls in,
  source file, first strings). Ghidra's `DumpDecomp.java` adds these as
  function starts that auto-analysis missed.

Generated decompiler output goes to `.work/ghidra/<prog>.c` (not committed):
grep it, or print one function with `tools/ghidra/fn.sh <prog> 0xADDR`.

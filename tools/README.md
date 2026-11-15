# tools/

Python 3 with `capstone unicorn pillow` (`python3 -m venv .work/venv &&
.work/venv/bin/pip install capstone unicorn pillow`). Run with the venv's
python. Every tool prints its usage with no arguments.

| Tool | Does |
|---|---|
| `mz.py FILE...` | MZ header at the right offsets, entry, image size, relocs, bound LE/NE/PE, packer and compiler strings, sha256 |
| `dosrun.py` | drive the dos-re-suite DOSBox-X from scripts: start/stop on Xvfb, type, key (real hold), shot, mem, find, poke, regs, save/load, `locate` (LE deltas) |
| `dosrun.py runto 0xLIN out.bin [secs] [line]` | break at a linear address (a packed program's real entry), dump RAM 0..0xA0000 there |
| `real/disasm16.py DUMP CS START [END\|+N]` | 16-bit disassembly from a RAM dump or a `.state`, targets as `CS+xxxx` |
| `real/unlzexe.py PACKED.EXE OUT.EXE` | LZEXE 0.91 to a plain MZ (entry, stack, relocations) for Ghidra/REA; checked against RAM on Dave |
| `real/statedump.py STATE ...` | read RAM out of a DOSBox-X save state: `--get`, `--slurp`, `--find`, `--seg` |
| `real/raw2png.py RAW PAL OUT [w] [scale]` | indexed framebuffer to PNG (6-bit DAC palettes scaled ×4) |
| `le/leparse.py EXE OUT` | LE (DOS/4GW) loader: applies fixups, writes `OUT.elf` (for Ghidra/REA/objdump), `OUT.json`, `OUT.objN.bin` |
| `le/disasm.py PROG ADDR [N\|--func]` | 32-bit disassembly at image addresses, strings and names annotated |
| `le/funcs.py PROG` | function list by recursive descent + call seeding; Watcom `__FILE__` source names |
| `le/xref.py PROG 0xADDR\|'text'` | code references to an address or to strings containing text |
| `le/emu.py` | run single functions under Unicorn with Python stubs for callees (Watcom convention) |
| `ghidra/decomp.sh PROG` | Ghidra headless: apply `analysis/PROG_names.txt`, add `PROG_funcs.tsv` starts, dump all to `.work/ghidra/PROG.c` |
| `ghidra/fn.sh PROG 0xADDR...` | print one function from that dump |
| `tp/tpc.sh TPDIR SRCDIR FILE.PAS` | compile with Turbo Pascal under stock DOSBox, headless [SAM2] |
| `tp/rtlcmp.py TARGET.EXE REF[,REF..] [SEG..]` | classify target routines against reference builds of one compiler: `same` (layout bytes as wildcards), `layout` (only numbers differ), `differ`; with `tp/rtltest/*.PAS` (built by `tp/tpc.sh`) it pins the Turbo Pascal version [SAM2] |
| `tp/inventory.py UNPACKED.EXE OUT.json` | Turbo Pascal MZ: segments from the relocated words, routines by recursive descent, `frame_only` candidates; writes a matching-decomp recipe skeleton [SAM2] |
| `wc/libmatch.py PROG LIB [LIB..]` | Watcom/OMF: which library modules occur byte for byte (fixups masked) in an LE program; compares patch levels to pin the compiler version [wacky] |

LE workflow:

```sh
tools/le/leparse.py original/GAME.EXE .work/le/game
tools/le/funcs.py game > analysis/game_funcs.tsv
tools/le/xref.py game 'some string'
tools/le/disasm.py game 0x15a14 --func
TMPDIR=<dir without dots> tools/ghidra/decomp.sh game   # GHIDRA_INSTALL_DIR
```

Real-mode workflow: boot under the harness, `dump_memory 0 0 655360` (or a
`save_state`), find CS/DS for this boot, then `real/disasm16.py`. For Ghidra on
a 16-bit MZ, import the EXE directly; Ghidra's `FUN_seg_off` uses its own
segment numbering: convert through the CS offset, never pass Ghidra's
addresses to gdb.

Name per-session one-off probes `tools/probes/<topic>.py`; promote a probe to
`tools/` when a check or a second session uses it.

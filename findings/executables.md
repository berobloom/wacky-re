# Executables

## WW.EXE (the game)

| Fact | Value | Level, evidence |
|---|---|---|
| Release | Wacky Wheels 1.1, registered (CD, files dated 1996-12-24) | CONFIRMED: string `WACKY WHEELS VERSION 1.1`; `original/SHA256SUMS` (sha256 376260464dd60c1a...) |
| Format | MZ stub with DOS/4GW (Rational 1990-1994) bound in front of the program's own MZ at file 0x26654; its LE header at 0x290FC, page offsets relative to that inner MZ | CONFIRMED: `tools/mz.py`, `tools/le/leparse.py` (bound support added for this) |
| Objects | obj1 code at 0x10000, 0x4BE2E bytes; obj2 data at 0x60000, 0x33100 bytes (stack at its end); entry 0x411A8 | CONFIRMED: `.work/le/ww.json` |
| Compiler | Watcom C/C++32 | CONFIRMED: run-time strings `WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1993` |
| Version | **9.5b** (9.5 with patch levels A and B) | STRONGLY SUPPORTED: `tools/wc/libmatch.py ww` against CLIB3R.LIB of 9.5 / 9.5a / 9.5b: 72 / 84 / 93 modules found byte for byte (fixup bytes masked). 12 modules occur only from 9.5a on, 9 more only in 9.5b (`exit` 0x42C10, `fread` 0x42C68, `fseek` 0x42E91, `signal` 0x431FA, `scnf` 0x54651, `grownear`, `allocfp`, `grabfp87`, `sigsys`); none favours an older level. Not yet excluded: a later 9.5 patch or 10.0 (copyright 1988-1993 argues against 10.0) |
| Model | flat, register calling convention (`3r` library) | LIKELY: the 3R library matches; function prologues save registers (`push ebx/ecx/edx/esi/edi`) |
| Options | **`-s -3r -oilrt`**, possibly with more of `-oaemn` (`-oxt` = `-omilerst`+`-t`, and Watcom's own `-oneatx`, give the same bytes) | STRONGLY SUPPORTED: `analysis/optpin/check.sh` compiles probe C for 11 game functions; 10 match byte for byte at their addresses (fixup bytes masked). Each flag is needed: without `-s` 2/4 of T1; without `-r` 7/11 (`-oilt`; `-or` changes scheduling, e.g. `movzx` pairs before the shift); without `-l` find12 0x16A7C keeps a counter instead of a byte offset stepping by 12; without `-i` or `-t` sendstr 0x109D8 differs (153 instead of 188 bytes; `-t` keeps the hoisted constant 10 in `ebp`). `-oi` also fits the whole program: 213 inline `repne scasb` (strlen) and 200 `rep movsd` in game code, and no game call to clib's `strlen`/`memcpy`/`strcpy`/`inp`/`outp`. `-5r` and `-os` fail; `-d2`/`-od` fail. Not yet told apart: `-oa`, `-oe`, `-om`, `-on`; struct packing (`-zp`) |
| Code shape | 871 functions found, 282 KB of 310 KB decoded; 574 start with Watcom register saves, none with a stack check (`-s`); 12 large `pushad` routines (about 30 KB) look hand-written asm | LIKELY: `tools/le/funcs.py ww`, prologue count |

## Toolchain on disk (not committed)

`.work/toolchains/inst/WC95`, `WC95A`, `WC95B`: Watcom C/C++ 9.5 from WinWorld's "Watcom CPP 9.5b (1993)"
floppy set (W9532 disks), installed with the original INSTALL.EXE under DOSBox, and patched with the set's
`Patch32.zip` (APPLYA.BAT, then APPLYB.BAT). How: `harness/TRAPS.md`, "Watcom 9.5 install".

## Open

- Options left open: `-oa` (aliasing), `-oe` (inlining user functions), `-om`, `-on` (floating point), `-zp`. Each
  needs a game function where it changes the code: `-oe` a small function called in its own module, `-zp` a struct
  with a hole, `-oa` code that reads through a pointer after a store.
- crcstep 0x102CC: no C form found yet (QUESTIONS.md 1).
- Map the library part of obj1 (where clib starts, link order) and the asm routines. The lowest clib module found
  is `strncmp` at 0x41450 (`tools/wc/libmatch.py -l ww .../CLIB3R.LIB`), so the game's C ends below that.

## Probe functions (the option evidence)

`analysis/optpin/*.C`, compiled by `check.sh`; the names are ours (LIKELY from what the code does).

| Address | Probe | What it does |
|---|---|---|
| 0x109D8 | sendstr (T6) | sends a string byte by byte through 0x105FC, waits 10 ticks of 0x7E720 after each, then CR and LF; stops with 0x7EBB0 = 1 when 0x88CD0 == 1 |
| 0x10B74 | getit (T2) | calls 0x41475, stores a non-zero result through a pointer |
| 0x12490 | fromscroll (T1) | `int` global minus the argument |
| 0x14218, 0x1422C, 0x1424C | seekdat, telldat, readdat | `fseek`/`ftell`/`fread` on the FILE* at 0x7D584 |
| 0x14238 | get22 | `long` at +0x12 of a 22-byte record, indexed by a `short` global |
| 0x16A7C | find12 | linear search of 8 12-byte records for (x, y), returns the record's first `short` or -1 |
| 0x248AC | setvmode | `int386(0x10)` with AH=0, AL=mode |
| 0x341D8 | waitkey | drains `kbhit`/`getch`, prints "Press any key when ready..", `getch` |
| 0x102CC | crcstep | CRC table step (the table at 0x7D380 is built by 0x10298 through 0x10260); **differs** |


## Traps

- Probe one function per file when the code is not yet matching: Watcom merges identical function bodies (two
  publics, one offset) and jumps into another function's identical tail, so variants in one file change each other.
- `objmatch.py` without an address finds only functions with a run of 4 bytes no fixup touches; a 10-byte
  `mov eax,[x]; jmp f` needs its address given.

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
| Code shape | 871 functions found, 282 KB of 310 KB decoded; 574 start with Watcom register saves, none with a stack check (`-s`); 12 large `pushad` routines (about 30 KB) look hand-written asm | LIKELY: `tools/le/funcs.py ww`, prologue count |

## Toolchain on disk (not committed)

`.work/toolchains/inst/WC95`, `WC95A`, `WC95B`: Watcom C/C++ 9.5 from WinWorld's "Watcom CPP 9.5b (1993)"
floppy set (W9532 disks), installed with the original INSTALL.EXE under DOSBox, and patched with the set's
`Patch32.zip` (APPLYA.BAT, then APPLYB.BAT). How: `harness/TRAPS.md`, "Watcom 9.5 install".

## Open

- Pin the compiler options (`-s`, `-3r`/`-5r`, `-o...`, `-zp`) by compiling candidate C for a few small game
  routines and comparing bytes.
- Map the library part of obj1 (where clib starts, link order) and the asm routines.

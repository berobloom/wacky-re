# Executables

## WW.EXE (the game)

| Fact | Value | Level, evidence |
|---|---|---|
| Release | Wacky Wheels 1.1, registered (CD, files dated 1996-12-24) | CONFIRMED: string `WACKY WHEELS VERSION 1.1`; `original/SHA256SUMS` (sha256 376260464dd60c1a...) |
| Format | MZ stub with DOS/4GW (Rational 1990-1994) bound in front of the program's own MZ at file 0x26654; its LE header at 0x290FC, page offsets relative to that inner MZ | CONFIRMED: `tools/mz.py`, `tools/le/leparse.py` (bound support added for this) |
| Objects | obj1 code at 0x10000, 0x4BE2E bytes; obj2 data at 0x60000, 0x33100 bytes (stack at its end); entry 0x411A8 | CONFIRMED: `.work/le/ww.json` |
| Compiler | Watcom C/C++32 | CONFIRMED: run-time strings `WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1993` |
| Version | **9.5b** (9.5 with patch levels A and B) | STRONGLY SUPPORTED: `tools/wc/libmatch.py ww` against CLIB3R.LIB of 9.5 / 9.5a / 9.5b: 72 / 84 / 93 modules found byte for byte (fixup bytes masked). 12 modules occur only from 9.5a on, 9 more only in 9.5b (`exit` 0x42C10, `fread` 0x42C68, `fseek` 0x42E91, `signal` 0x431FA, `scnf` 0x54651, `grownear`, `allocfp`, `grabfp87`, `sigsys`); none favours an older level. Not yet excluded: a later 9.5 patch or 10.0 (copyright 1988-1993 argues against 10.0) |
| Model | flat, register calling convention (`3r` library) | LIKELY: the 3R library matches; function prologues save registers (`push ebx/ecx/edx/esi/edi`). CONFIRMED order (session 2026-08, find12 0x16A7C probe): arg1=EAX, arg2=EDX, arg3=ECX, arg4=EBX |
| Options | **`-s -3r -oailrt`**, possibly with `-oe`, `-om`, `-on` (Watcom's documented fastest setting `-oneatx` = `-onea` + `-ox` + `-t`, and `-oatx`, give the same bytes; `-oneatx` is LIKELY the command line) | STRONGLY SUPPORTED: `analysis/optpin/check.sh` compiles probe C for 11 game functions, all match byte for byte at their addresses (fixup bytes masked), and `src/` CRC code adds 3 more. Each flag is needed: without `-s` 2/4 of T1; without `-r` 7/11 (`-r` changes scheduling, e.g. `movzx` pairs before the shift); without `-l` find12 0x16A7C keeps a counter instead of a byte offset stepping by 12; without `-i` or `-t` sendstr 0x109D8 differs (153 instead of 188 bytes; `-t` keeps the hoisted constant 10 in `ebp`); without `-a` crchware 0x10260 stores its `static` loop counter every pass instead of keeping it in `esi`, and crcupdate 0x102CC allocates other registers. `-oi` also fits the whole program: 213 inline `repne scasb` (strlen) and 200 `rep movsd` in game code, and no game call to clib's `strlen`/`memcpy`/`strcpy`/`inp`/`outp`. `-5r`, `-os`, `-d2`, `-od` fail. Not yet told apart: `-oe`, `-om`, `-on`. Struct packing: **pack(1) CONFIRMED** for game structs: `players` has a dword field at +0x5E (misaligned for natural alignment; store at 0x1720B `mov %edx,0x6aaee(%eax)`), struct size 0x68; port uses `#pragma pack(1)` |
| Code shape | 871 functions found, 282 KB of 310 KB decoded; 574 start with Watcom register saves, none with a stack check (`-s`); 12 large `pushad` routines (about 30 KB) in the asm block (Code layout below) | LIKELY: `tools/le/funcs.py ww`, prologue count. The 11 unaligned "functions" below 0x36A4E (0x1150B, 0x17126, ...) start mid-instruction: misdetections |

## Code layout (obj1)

| Range | What | Level, evidence |
|---|---|---|
| 0x10000-0x1000F | `BEGTEXT` (`int3; jmp $-1`, nops) | CONFIRMED: the same 16 bytes head our own WLINK 9.5b link (`.work/linktest`) |
| 0x10010-0x1025F | initialisers of **local arrays** of the game module (Watcom 9.5 keeps them at the start of the module's `_TEXT`, in function order, and copies them with `rep movsd`): 0x10010 (27 names x 12: drivers then sounds) for 0x12124, 0x10154 (months) for the function around 0x1C9A0, then class names, screen names, and so on. Contents: 12-byte driver names (UNO, SULTAN, MORRIS, PEGGLES, RAZER, RINGO, BLOMBO, TIGI), sound names, months, class names ("AMATEUR CLASS"), screen names | CONFIRMED: 0x12124 `mov ecx,0x51; mov esi,0x10010; rep movsd` to its stack frame; Watcom 9.5b also puts initialised `const` arrays there (probe `D.C`); string literals go to CONST in the data object (no `-zc`: "Press any key when ready.." is at 0x61E80) |
| 0x10260-0x36A4D | **the game's C: one module**, about 222 functions, all 4-aligned | STRONGLY SUPPORTED: WLINK starts every compiled module on 16 bytes (`_TEXT` SEGDEF align = paragraph) and fills the gap with zeros (seen in our link); the whole range has no `ret`-then-zeros gap, where separate modules would leave one in 15 of 16 cases. Also no duplicated string literal in the game's strings (Watcom pools literals per module). Probably one source file or a unity build (`#include` of the .c files); not told apart |
| 0x36A4E-0x409EE | **assembler**, about 40 KB, starts not 4-aligned, `xor ecx,ecx` as `33 c9` (the C compiler emits `31`); the 12 `pushad` routines (VGA port writes to 0x3C4/0x3CE, two unrolled routines of 11 and 18 KB), interrupt handlers (`iret`; the one at about 0x409C0 sets the abort flag 0x88CD0), `cli; ret` / `sti; ret` at 0x409EB / 0x409ED | LIKELY: alignment and encoding; directly after the C (0x36A49 `jmp exit` ends `main`?), no gap, so its segment is byte-aligned. Assembler (TASM, WASM, MASM) not known |
| 0x409EF- | libraries, in WLINK's resolve order: clib modules (byte-aligned, no gaps: `int386` 0x409EF, `_dos_getvect`, `_dos_setvect`, `strncmp` 0x41450, ...) mixed with paragraph-aligned compiled modules (zero gaps at 0x40A80, 0x41510, 0x41F30, ...) | CONFIRMED for the 93 clib modules (`libmatch.py -l`); the paragraph modules include the **Apogee Sound System** (GUSWAVE, Multivoc strings at 0x62890 / 0x62A1B, used from 0x47726 / 0x48782) |

For the rebuild: one C file in address order (tables first), one asm file, then the libraries, which WLINK
places by itself if the same libraries are given in the same order.

## Toolchain on disk (not committed)

`.work/toolchains/inst/WC95`, `WC95A`, `WC95B`: Watcom C/C++ 9.5 from WinWorld's "Watcom CPP 9.5b (1993)"
floppy set (W9532 disks), installed with the original INSTALL.EXE under DOSBox, and patched with the set's
`Patch32.zip` (APPLYA.BAT, then APPLYB.BAT). How: `harness/TRAPS.md`, "Watcom 9.5 install".

## Open

- Options left open: `-oe` (inlining user functions), `-om`, `-on` (floating point), `-zp`. Each needs a game
  function where it changes the code: `-oe` a small function called in its own module, `-zp` a struct with a hole.
- `-oe`, `-om`, `-on`: settled by the first function that differs under one of them.
- The paragraph-aligned library modules after 0x409EF: which belong to the Apogee Sound System, which to other
  libraries; whether the ASS source of a matching version is available (not found in a first search).
- The assembler of 0x36A4E-0x409EE, and whether it is one module.

## Probe functions (the option evidence)

`analysis/optpin/*.C`, compiled by `check.sh`; the names are ours (LIKELY from what the code does).

| Address | Probe | What it does |
|---|---|---|
| 0x109D8 | sendstr (T6) | sends a string byte by byte through 0x105FC, waits 10 ticks of 0x7E720 after each, then CR and LF; stops with 0x7EBB0 = 1 when 0x88CD0 == 1 |
| 0x10B74 | xalloc (T2) | `malloc` (clib `nmalloc` 0x41475), -1 when it fails, else stores the block through a pointer |
| 0x12490 | fromscroll (T1) | `int` global minus the argument |
| 0x14218, 0x1422C, 0x1424C | seekdat, telldat, readdat | `fseek`/`ftell`/`fread` on the FILE* at 0x7D584 |
| 0x14238 | get22 | `long` at +0x12 of a 22-byte record, indexed by a `short` global |
| 0x16A7C | find12 | linear search of 8 12-byte records for (x, y), returns the record's first `short` or -1 |
| 0x248AC | setvmode | `int386(0x10)` with AH=0, AL=mode |
| 0x341D8 | waitkey | drains `kbhit`/`getch`, prints "Press any key when ready..", `getch` |
| 0x102CC | crcstep (crcupdate in `src/`) | CRC table step: `idx = (crc >> 8) ^ b; crc = (crc << 8) ^ table[idx]` (table 0x7D380, built by 0x10298 through 0x10260) |


## Traps

- Probe one function per file when the code is not yet matching: Watcom merges identical function bodies (two
  publics, one offset) and jumps into another function's identical tail, so variants in one file change each other.
- `objmatch.py` without an address finds only functions with a run of 4 bytes no fixup touches; a 10-byte
  `mov eax,[x]; jmp f` needs its address given.

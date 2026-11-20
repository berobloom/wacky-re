# Executables

## WW.EXE (the game)

| Fact | Value | Level, evidence |
|---|---|---|
| Release | Wacky Wheels 1.1, registered (CD, files dated 1996-12-24) | CONFIRMED: string `WACKY WHEELS VERSION 1.1`; `original/SHA256SUMS` (sha256 376260464dd60c1a...) |
| Format | MZ stub with DOS/4GW (Rational 1990-1994) bound in front of the program's own MZ at file 0x26654; its LE header at 0x290FC, page offsets relative to that inner MZ | CONFIRMED: `tools/mz.py`, `tools/le/leparse.py` (bound support added for this) |
| SETUP.EXE | Also a bound DOS/4GW LE (LE header at 0x290FC, Watcom C/C++32 run-time, "WACKY WHEELS VER 1.1 setup. Copyright BEAVIS-SOFT 1994"), not a 16-bit MZ program | CONFIRMED: `tools/mz.py`, `tools/le/leparse.py original/SETUP.EXE .work/le/setup`; what it writes: findings/formats.md "SETUP.EXE" |
| Objects | obj1 code at 0x10000, 0x4BE2E bytes; obj2 data at 0x60000, 0x33100 bytes (stack at its end); entry 0x411A8 | CONFIRMED: `.work/le/ww.json` |
| Compiler | Watcom C/C++32 | CONFIRMED: run-time strings `WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1993` |
| Version | **9.5b** (9.5 with patch levels A and B) | STRONGLY SUPPORTED: `tools/wc/libmatch.py ww` against CLIB3R.LIB of 9.5 / 9.5a / 9.5b: 72 / 84 / 93 modules found byte for byte (fixup bytes masked). 12 modules occur only from 9.5a on, 9 more only in 9.5b (`exit` 0x42C10, `fread` 0x42C68, `fseek` 0x42E91, `signal` 0x431FA, `scnf` 0x54651, `grownear`, `allocfp`, `grabfp87`, `sigsys`); none favours an older level. Not yet excluded: a later 9.5 patch or 10.0 (copyright 1988-1993 argues against 10.0) |
| Model | flat, register calling convention (`3r` library) | LIKELY: the 3R library matches; function prologues save registers (`push ebx/ecx/edx/esi/edi`). CONFIRMED order: arg1=EAX, arg2=EDX, arg3=EBX, arg4=ECX (fn_173c4 fread call at 0x17405/0x17426: ptr/size/count/stream land in EAX/EDX/EBX/ECX; fn_16d68 3-arg probe agrees) |
| Options | **`-s -3r -oailrt`**, possibly with `-oe`, `-om`, `-on` (Watcom's documented fastest setting `-oneatx` = `-onea` + `-ox` + `-t`, and `-oatx`, give the same bytes; `-oneatx` is LIKELY the command line) | STRONGLY SUPPORTED: `analysis/optpin/check.sh` compiles probe C for 11 game functions, all match byte for byte at their addresses (fixup bytes masked), and `src/` CRC code adds 3 more. Each flag is needed: without `-s` 2/4 of T1; without `-r` 7/11 (`-r` changes scheduling, e.g. `movzx` pairs before the shift); without `-l` find12 0x16A7C keeps a counter instead of a byte offset stepping by 12; without `-i` or `-t` sendstr 0x109D8 differs (153 instead of 188 bytes; `-t` keeps the hoisted constant 10 in `ebp`); without `-a` crchware 0x10260 stores its `static` loop counter every pass instead of keeping it in `esi`, and crcupdate 0x102CC allocates other registers. `-oi` also fits the whole program: 213 inline `repne scasb` (strlen) and 200 `rep movsd` in game code, and no game call to clib's `strlen`/`memcpy`/`strcpy`/`inp`/`outp`. `-5r`, `-os`, `-d2`, `-od` fail. Not yet told apart: `-om`, `-on`. `-oe` CONFIRMED needed (see Open). Struct packing: **pack(1) CONFIRMED** for game structs: `players` has a dword field at +0x5E (misaligned for natural alignment; store at 0x1720B `mov %edx,0x6aaee(%eax)`), struct size 0x68; port uses `#pragma pack(1)` |
| Code shape | 871 functions found, 282 KB of 310 KB decoded; 574 start with Watcom register saves, none with a stack check (`-s`); 12 large `pushad` routines (about 30 KB) in the asm block (Code layout below) | LIKELY: `tools/le/funcs.py ww`, prologue count. The 11 unaligned "functions" below 0x36A4E (0x1150B, 0x17126, ...) start mid-instruction: misdetections |

## Code layout (obj1)

| Range | What | Level, evidence |
|---|---|---|
| 0x10000-0x1000F | `BEGTEXT` (`int3; jmp $-1`, nops) | CONFIRMED: the same 16 bytes head our own WLINK 9.5b link (`.work/linktest`) |
| 0x10010-0x1025F | initialisers of **local arrays** of the game module (Watcom 9.5 keeps them at the start of the module's `_TEXT`, in function order, and copies them with `rep movsd`): 0x10010 (27 names x 12: drivers then sounds) for 0x12124, 0x10154 (months) for the function around 0x1C9A0, then class names, screen names, and so on. Contents: 12-byte driver names (UNO, SULTAN, MORRIS, PEGGLES, RAZER, RINGO, BLOMBO, TIGI), sound names, months, class names ("AMATEUR CLASS"), screen names | CONFIRMED: 0x12124 `mov ecx,0x51; mov esi,0x10010; rep movsd` to its stack frame; Watcom 9.5b also puts initialised `const` arrays there (probe `D.C`); string literals go to CONST in the data object (no `-zc`: "Press any key when ready.." is at 0x61E80) |
| 0x10260-0x36A4D | **the game's C: one module**, about 222 functions, all 4-aligned | STRONGLY SUPPORTED: WLINK starts every compiled module on 16 bytes (`_TEXT` SEGDEF align = paragraph) and fills the gap with zeros (seen in our link); the whole range has no `ret`-then-zeros gap, where separate modules would leave one in 15 of 16 cases. Also no duplicated string literal in the game's strings (Watcom pools literals per module). Probably one source file or a unity build (`#include` of the .c files); not told apart |
| 0x36A4E-0x409EE | **assembler**, about 40 KB, starts not 4-aligned, `xor ecx,ecx` as `33 c9` (the C compiler emits `31`); the 12 `pushad` routines (VGA port writes to 0x3C4/0x3CE, two unrolled routines of 11 and 18 KB), interrupt handlers (`iret`; the one at about 0x409C0 sets the abort flag 0x88CD0), `cli; ret` / `sti; ret` at 0x409EB / 0x409ED | LIKELY: alignment and encoding; directly after the C (0x36A49 `jmp exit` ends `main`?), no gap, so its segment is byte-aligned. Assembler (TASM, WASM, MASM) not known. **Rebuilt (iter 90): `src/wwasm.asm`** reassembles to these 40,865 bytes exactly and links in their place (below) |
| 0x409EF- | libraries, in WLINK's resolve order: clib modules (byte-aligned, no gaps: `int386` 0x409EF, `_dos_getvect`, `_dos_setvect`, `strncmp` 0x41450, ...) mixed with paragraph-aligned compiled modules (zero gaps at 0x40A80, 0x41510, 0x41F30, ...) | CONFIRMED for the 93 clib modules (`libmatch.py -l`); the paragraph modules include the **Apogee Sound System** (GUSWAVE, Multivoc strings at 0x62890 / 0x62A1B, used from 0x47726 / 0x48782) |

For the rebuild: one C file in address order (tables first), one asm file, then the libraries, which WLINK
places by itself if the same libraries are given in the same order.

## Data layout (obj2)

| Range | What | Level, evidence |
|---|---|---|
| 0x60000-0x60002 | three bytes `01 01 01`, then a NUL: before the game module's CONST group | CONFIRMED the bytes, UNKNOWN what they are: no dword in either object holds 0x60001/0x60002/0x60003, four hold 0x60000 (0x35580, 0x47692, 0x476c6, 0x4d636), so a pointer to the byte at 0x60000 exists but nothing reads the other two |
| 0x60004-0x6235A | **the game module's CONST group**: 9046 bytes of pooled string literals, ending with the `%d WWSW.BIN` / `WWREG.BIN` pair | CONFIRMED: `src/check.sh`'s fixup census puts all 327 of our references to it on one base, 0x60004, and `tools/wc/datamatch.py` derives the same base independently (1107 of its 16-byte anchors land there). **Byte-for-byte equal to our build's group at every byte something reads**: 25 of 9046 bytes differ and no dword in the image holds any of those 25 addresses, so they are dead space left by the linker's literal pooling - the game's pool runs `tlyMUSIC PAUSED` (0x60045), `nceCONNECTION TERMINATED` (0x60055), `CoWACK`/`ack` (0x600a2/0x600a9), `//BAD CRC  =` (0x6006e) where our build has NUL padding, the same sharing findings/menus.md saw for `""` at 0x600ac. Each shared literal's own bytes are identical at the same address, so this is not behaviour |
| 0x6235C-0x64F7E | 7869 initialized bytes in 43 regions our build emits no group for (`datamatch.py`, `-v` lists them) | CONFIRMED the census. Two owners: **the libraries** - the Apogee Sound System's error strings 0x6235C-0x631D1, DOS/4GW's math library 0x63004-0x63147 (`GF1PATCH110`, `acos`, `RATIONAL DOS/4G`), clib's `_iob` at 0x6400C - which is right, the real libraries are linked in; and **the game's own C data**: the credits table (`credits[][0x2c]`, `src/ww.c:8086`: `PROGRAMMING AND DESIGN` 0x632F0, `ANDREW EDWARDSON` 0x6331C, ~20 regions out to 0x63D5D) and the race-result praise strings 0x63D44 / 0x63E0C / 0x63ED4 (check.sh's `unnamed arr63d44/arr63e0c/arr63ed4`). Our C declares those `extern` and nothing defines them: the hybrid link keeps the game's obj2 so they are there at run time, and a pure-C link has to supply them (QUESTIONS 23). LIKELY that those are the whole game-C part of obj2 - the census is checked, no source-level list is |

## Toolchain on disk (not committed)

`.work/toolchains/inst/WC95`, `WC95A`, `WC95B`: Watcom C/C++ 9.5 from WinWorld's "Watcom CPP 9.5b (1993)"
floppy set (W9532 disks), installed with the original INSTALL.EXE under DOSBox, and patched with the set's
`Patch32.zip` (APPLYA.BAT, then APPLYB.BAT). How: `harness/TRAPS.md`, "Watcom 9.5 install".

## Open

- **`-oe` is CONFIRMED** (2025 session): the full-build function set matches only with `e`
  (com_open 0x102F4 spill/frame pattern fails without it, 71 vs 82 identical). `e` inlines small
  user functions (usage string in WCC386.EXE: `e[=<num>] -> expand user functions inline. <num>
  controls max size`); every optinfo set that keeps com_open also inlines `readdat` into fn_17928,
  while the game keeps `call 0x1424C` at 24 sites (0x18215, 0x18232, 0x18525, 0x194DB, 0x27C3A,
  0x2CA3B, 0x345FB, ...). `-oae=N` with N<=4 stops readdat's inlining but loses com_open et al.
  No pragma, aux form, declaration style or address-taken use suppresses it (all probed; 9.5b has
  no inline-asm keyword). Open question: what in the game's source prevented it — fn_17928/fn_1832c
  stay near-misses until found.
- Optinfo letters accepted by 9.5b (usage line in WCC386.EXE): `a,c,d,e,f[+],i,l,m,n,o,p,r,s,t,u,x`;
  `a`=relax aliasing, `d`=disable all, `c`=disable call-then-return->jump, `m` breaks com_open (never
  needed), `i/l/r` also break com_open — full-build evidence shows the optpin probes for `-oi/-ol/-or`
  were probe-context artifacts (sendstr/find12/crchware match under `-oneatx` in the full build).
- `-om`, `-on` (floating point), `-zp` still open.
- The paragraph-aligned library modules after 0x409EF: **the Apogee Sound System and the Gravis UltraSound
  SDK it bundles.** Jim Dosé's released ASS source (github.com/jimdose/Apogee_Sound_System, GPL v2, cloned
  to `.work/ext/`, not committed) ships `AUDIO_WF.LIB`: `tools/wc/libmatch.py -l ww` finds 21 of its 59
  modules byte for byte - all the GUS SDK (`gf1*.c`, `gf1*.asm`, `os.c`, `timer.c`, `digital.c`) at
  0x4dd30-0x56f9x (CONFIRMED). Of ASS's own 386 functions only 25 are found whole (18 in `pas16.c`, one each
  in `task_man.c` 0x40ff0, `sndscape.c`, `gus.c`, `blaster.c` x3), so the game has an earlier release of
  the same code (STRONGLY SUPPORTED: same function names - `MUSIC_FadeVolume` 0x42488, `TS_*`, `FX_*` - and
  the strings). The shipped LIB was built `/omaxnet /zp4 /5r` (`AUDIO.MAK`); the game's build options are
  open. **Closest known release: ASS 1.04** (github.com/gamesrc-ver-recreation/audiolib, which
  reconstructs 1.04 / 1.09 / 1.1 / 1.12 from the ROTT, Shadow Warrior, Duke 3D and Blood EXEs and keeps
  their original LIBs in `origlibs/`): per-function search of each original LIB in WW.EXE finds 200 of 471
  functions for 1.04, 198 for 1.09, 181 for 1.1 and 1.12; ASS's own code (without the GUS SDK) 96 of 341
  for 1.04 - whole `mpu401.c`, `dpmi.c`, `ll_man.c`, most of `pcfx.c`, `sndsrc.c`, `blaster.c` 16/46,
  but `fx_man.c`, `guswave.c`, `multivoc.c` 2/33 and `music.c` 1/14 differ (CONFIRMED counts, iter 56c).
  LIKELY: the game has an ASS release before 1.04 (Wacky Wheels is 1994, ROTT 1.3 later).
  **Iter 92, function by function over all six original LIBs** (`tools/wc/assmatch.py`, which also
  writes the names): exact hits per version - 1.04 231 functions (37,365 bytes), 1.09 229, ROTTB 211,
  1.1/1.12 212, ROTTD 153 - so the two ROTT pre-release LIBs are *further* from the game than 1.04
  (CONFIRMED counts); the union over all six is 228 distinct addresses after resolving shared bodies
  (e.g. `BLASTER_ReadMixer` = `ga_read`, the module with more neighbours wins) plus 32 anchored near
  matches at `funcs.py` starts (LIKELY, score >= 0.6). All are in analysis/ww_names.txt's ASSMATCH
  block. `lib_40ff0` was `TS_SetTaskRate` (CONFIRMED, renamed in src/ww.c).
- **Watcom `MATH387R.LIB` 9.5b**: 21 modules, 4,745 bytes byte for byte (`libmatch.py`; pow387,
  exp387, strtod, ftos, log387, ...); the other 9.5b LIBs add nothing (GRAPH, CPLX*, PLIB*, MATH3*,
  EMU387 12 bytes, CLIB3S). CONFIRMED.
- **The 14 KB block at 0x57E2A is Watcom 9.5b's 387 emulator** (`EMU387.LIB` module `emu387`, 12,838
  bytes at 0x57E2C): CONFIRMED for the code - every byte from +0x77 equals the LIB; the 68 that
  differ (+0xa..+0x76) are zero in the LIB and filled in the game, a table. `libmatch.py` cannot see
  it because it needs every byte equal. The 9.5/9.5a EMU387 shares only 432 bytes, so this is one
  more pin on 9.5b.
- Still unidentified: 38,489 bytes, mostly inside the ASS range (0x41504 3 KB before
  `MUSIC_SetVolume`, 0x49260 2 KB in multivoc, 0x4b47c 2 KB in midi.c): the ASS modules no released
  LIB matches exactly. By module (1.04 functions >= 24 bytes still unplaced; some may be unlinked):
  multivoc.c 8.4 KB, al_midi.c 5.7, sndscape.c 5.0, guswave.c 4.5, blaster.c 4.1, midi.c 3.9,
  fx_man.c 3.5; the GF1 modules only ~2.4 KB (gf1sound, gf1note1, digpos, gf1pat1, gf1mixer, gf1isr).
  LIKELY not Gravis's GUSDK 2.22 low-level toolkit (`Ultra*`, github RobertSundling/Annotated-GUSDK,
  checked iter 92): ASS's GUS code is its own `C:\PRG\NEWGF1\GF1NEW\gf1*.c` library (module names
  in the LIBs), already matched there, and the gaps are in ASS's own modules.
  **Compiler test (iter 92e):** the 1.04 recreation source rebuilt with *our* wcc386 9.5b and 1.04's
  production flags matches fewer functions than the original 10.0b LIB in most modules (task_man 1
  vs 3, dpmi 0/5, pcfx 5/10, mpu401 6/10, pas16 12/18, sndsrc 7/11; multivoc 3/3), and only four
  new ones (`PAS_SetMixMode`, `PAS_GetCardInfo`, `USER_CheckParameter`, `USER_GetText`, CONFIRMED
  in the names file). So LIKELY the game's ASS was not built with 9.5b and these flags: the gaps are
  source revisions before 1.04 (the recreation's earliest is ASSREV 19950518; Wacky Wheels is late
  1994), not the compiler. Gravis's GUSDK 2.01/2.22 are 16-bit `Ultra*` toolkits, not the 32-bit
  gf1 library ASS links (checked iter 92e). No ASS older than 1.04 is known online (searched).
  **GF1OS source (iter 92f):** `gf1os.exe` in `GUS_DRIVERS_SOURCE.7z` (dk.toastednet.org/GUS/SOURCE
  CODE/, the Utopia Sound Division release; password in its disclaimer; kept in .work only, not
  committed) is Gravis's 32-bit GF1 library, Sept-Nov 1993 (`NEWGF1.H`, gf1note.c, digpos.c, ...,
  `WATCOM.MAK`: `wcc386p -zq -DNOSEM -d2 -mf -oiaxt`). CONFIRMED the same source as the game's GF1
  code (gf1_error_str: identical body and string offsets; gf1_mem_avail: same statements, `NOSEM`'s
  `pushf; cli; call gf1_enter1`), but no build with WC 9.5/9.5a/9.5b and these flags matches a single
  function byte for byte, and an instruction-level match (prologue/epilogue and operands ignored)
  finds only 3: the game's GF1 code was built by a later compiler - `push ebp; mov ebp,esp` encoded
  `55 89 E5` (9.5 writes `55 8B EC`), `push ebp` before the saved registers, `mov esp,ebp` before each
  `pop ebp`, locals kept on the stack. LIKELY Watcom 10.0, like the original ASS LIBs it matches.
  **Iter 92g, with Watcom 10.0b** (`.work/toolchains/inst/WC100B` = WinWorld's 10.0a ISO tree + the
  Jan 1995 `.b` patch, os2site.com `c_b.zip`): it rebuilds ASS 1.04's task_man/pcfx/dpmi/mpu401 from
  the recreation source byte for byte against the original 1.04 LIB, so it is the ASS LIBs' compiler
  (CONFIRMED). Yet GF1OS 1993 built with it (`-d2`, `-of`, plain) still matches 0-1 game functions:
  the 1993 GF1OS is an older revision than ASS's `NEWGF1\GF1NEW` (CONFIRMED by the misses, the
  compiler being right). 10.0a matches 1, the 10.0 prerelease (3/1994) 3. Dead end for byte matching.
  **The game's ASS objects come from two compilers (iter 93):** src/ass (the recreation source at
  ASSREV 19941101) built per module with 10.0b / 10.0a / 9.5b and compared at the named addresses
  (`tools/ass/asscheck.py`): USER matches 2/2 only with 9.5b (0/2 with 10.0x: 9.5b loads a global
  with `mov eax,[x]`, 10.0 with `mov edx,[x]`); IRQ 3/3 only with 10.0a (10.0b 2/3); every other
  module scores the same with 10.0a and 10.0b, and lower with 9.5b (DPMI 5 vs 0, MPU401 10 vs 6).
  STRONGLY SUPPORTED: built mostly with Watcom 10.0a (the .b patch is January 1995, after the game),
  with objects a make never rebuilt left from 9.5b. Baseline: 90 of 144 named functions identical.
  **TASK_MAN is an older task manager (iter 93, CONFIRMED: src/ass TASK_MAN.C now builds the whole
  module 0x40A80-0x411AC byte for byte):** a `volatile tasklist TaskList` {start, end} kept with
  ll_man.h's `LL_AddToHead`/`LL_AddToTail`/`LL_Remove` (1.04: a circular list with a sentinel),
  `USESTACK` on with a 1 KB timer stack, the ISR calling a separate task loop, no `TS_InInterrupt`,
  `atexit(TS_Shutdown)` registered once from TS_Startup, and `TS_FreeTaskList(tasklist list)` taking
  the list by value (its NULL stores hit the copy). 1.04 still carries the unused `tasklist` typedef.
  `volatile` was found by its codegen: without it the loop's first load is scheduled earlier.
  PCFX (iter 93c, whole module identical): `PCFX_Service`'s sample `value` is an `int` (1.04:
  `unsigned`; `sar` vs `shr`), and `PCFX_VoiceAvailable` does not exist yet. SNDSRC (whole module):
  `SS_Init(void)` (1.04 takes the sound card number; Tandy only via the command line), the service
  task runs at 500 Hz (1.04: 510), and `SS_ServiceInterrupt` has no 14-samples-per-tick limit and
  calls the callback once after the loop when a buffer finished (`bufferdone == TRUE`).
  DMA (iter 93d, whole module): `DMA_SetupTransfer(channel, address, length)` always programs
  auto-init read (mode 0x58; 1.04 takes a mode), and `DMA_EndTransfer` does not exist yet. GUS:
  its object is 9.5b's like USER's (GUS_Init's stack slots only come out right with 9.5b), it has no
  `GUS_ErrorString`, and `GUS_Init` neither checks the IRQ nor sizes GUS memory; `GUSWAVE_Shutdown`
  frees every voice unconditionally from a 0x54-byte VoiceNode (1.04's GUS.C, built without /zp4,
  sees 0x65 bytes): left until GUSWAVE pins the struct. That project
  builds with Watcom C 10.0b + TASM 3.1 and selects a version by `LIBVER_ASSREV`: the way to recreate
  the game's release. A start for matching the sound library from source.
- The assembler of 0x36A4E-0x409EE, and whether it is one module. What the rebuild (iter 90, `src/wwasm.asm`,
  `tools/asm/asmcheck.py`) settled: CONFIRMED that it is **self-contained code** - 14,700 instructions
  decode in one linear sweep with no undecodable byte; none of its calls or jumps leaves the range; its
  1567 LE fixups all point into the data object (1566 off32, one sel16 at 0x4090e: `mov bx, seg` for DS
  in kbd_isr 0x40907); nothing outside reaches it but the C module's 35 call targets and two off32s to
  kbd_isr (0x32b66/0x32b7d). STRONGLY SUPPORTED that the assembler was **MASM-compatible** (MASM, TASM or
  WASM, not nasm-style): all 3083 register-register ALU/mov ops use the direction-bit form (`8B C8`
  mov ecx,eax; `33 C9` xor ecx,ecx; `66 03 FA` add di,dx), and `cmp ax,1` is the accumulator form
  `66 3D 01 00`, never `83 /7`. Still open: which one, and how many source modules.

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
- A data group's base has to be voted over **every** occurrence of each anchor: searching only the first hit
  of every 16-byte anchor put our CONST group 4 bytes early and made 8225 of its 9046 bytes look wrong
  (`datamatch.py`, and `test_datamatch.py` pins the base at 0x60004).
- A data byte that differs is not behaviour until something reads it: the linker shares pooled literals, so
  the game's pool keeps the text an earlier literal left behind where our build has NUL padding. Check that
  some dword in the image holds the byte's address before calling it a bug (25 such bytes in the CONST group).

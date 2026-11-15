# DOS platform reference

Standard PC/DOS facts that came up again and again. These are about the
platform, not one game; a game's own use of them still needs its own evidence.
Facts seen in the earlier projects are tagged [cold] / [redcat]; untagged ones
are standard references, not checked there.

## Memory map (real mode, first MB)

| Linear | Contents |
|---|---|
| 0x00000–0x003FF | interrupt vectors, 4 bytes each (INT n at 4n: offset, segment) |
| 0x00400–0x004FF | BIOS data area: 0x449 video mode, 0x46C tick count (u32, 18.2 Hz), 0x417 shift flags, 0x41A–0x43D keyboard buffer |
| 0x00500–0x9FFFF | DOS, TSRs, the program (PSP, then the image) |
| 0xA0000–0xAFFFF | VGA graphics (mode 13h: 320 × 200, 1 byte per pixel at A000:0) |
| 0xB8000–0xBFFFF | colour text (80 × 25: char, attribute) |
| 0xC0000–0xEFFFF | adapter ROMs |
| 0xF0000–0xFFFFF | system BIOS (a PC halted here is in an idle or IRQ path) |

## Programs

- **PSP**: 256 bytes just below the image; DS = ES = PSP at entry; command
  tail at PSP:0x80 (length byte, then text, then CR). Image segment = PSP +
  0x10.
- **.COM**: loads at PSP:0x100, CS = DS = ES = SS = PSP.
- **MZ**: see `tools/mz.py`. Entry CS:IP at 0x14/0x16 relative to the image
  segment; relocations patch segment words by the load segment.
- **Packers** (LZEXE, PKLITE, EXEPACK): the entry is a decompression stub. Let
  it run under the emulator, then dump the unpacked image, instead of
  unpacking statically. Recipe [dave]: run to the title screen, dump RAM,
  find the program's PSP on the MCB chain (below), dump from
  (PSP + 0x10) × 16 to the end of its block. Segment words in the dump are
  already relocated by the load segment. Read strings from the dump: the file
  on disk is compressed and shows almost none.
- **LZEXE 0.91** header at the stub's CS:0: words original IP, CS, SP, SS
  (relative to the load segment), then sizes. [dave: CS:IP 0000:0000]
- **MCB chain**: each block starts with a paragraph: byte 'M' (more) or 'Z'
  (last), word owner PSP, word size in paragraphs; the next MCB is at
  MCB + 1 + size. A program's MCB is PSP − 1. The first MCB's segment is the
  word at (INT 21h AH=52h ES:BX) − 2, or scan for a plausible 'M' chain.
  [dave]
- **DOS extenders** (DOS/4GW, PMODE/W, DOS/32A): an MZ stub plus an LE/LX
  image at the offset in the header (often found by searching for `LE\0\0`).
  Flat 32-bit model; objects get fixups; at run time each object sits at its
  own delta from the image address. DOS/4GW reports a CPU exception as
  "error 2001" (e.g. a divide by zero) and the program exits (exit code 209
  seen). [redcat]
- **Overlays / bound data**: bytes after the MZ load image size.

## Video

- Mode 13h: 320 × 200 × 256, linear at A000:0; the screen is 4:3, so each
  pixel is 1.2 times as tall as wide (stretch 320 × 200 to 640 × 480 as 2 ×
  2.4 when porting).
- Mode X / unchained: four planes, port 0x3C4 map mask; byte at A000 is four
  pixels apart.
- VESA (INT 10h AX=4Fxx): banked 64 KB windows (0x101 = 640 × 480 × 256).
- DAC: write index to 0x3C8 then R, G, B to 0x3C9; read via 0x3C7. Values
  are **6-bit** (0..63): multiply by 4 (or `c << 2 | c >> 4`) for 8-bit.
- Retrace: poll 0x3DA bit 3 (vertical) for vsync waits. A game that waits
  once per pass runs at 70.09 Hz in mode 13h. [dave: 70.2 measured]
- PCX: 128-byte header, RLE (top two bits set = run), a 256-colour palette in
  the last 769 bytes (0x0C, then 768 bytes of 8-bit RGB).

## Timers

- PIT channel 0 at 1.193182 MHz; default divisor 0 (65536) gives IRQ 0 at
  18.2065 Hz (54.925 ms), which counts 0x46C. Games reprogram it (port 0x43
  mode, 0x40 count) for faster ticks; channel 2 drives the PC speaker. `in al,
  0x40` is a common cheap random source.
- `clock()` in Watcom C counts from INT 21h AH=2Ch and moves only with the
  BIOS tick: any delay built on it ends on a tick. [redcat] Other C libraries
  likely the same; check.
- INT 1Ah AH=0: read the tick count.

## Keyboard

- IRQ 1 / INT 9; port 0x60 gives the scancode, bit 7 set = release (break).
  Games often install their own INT 9 handler and keep a `held[scan]` table.
- Common set-1 scancodes: Esc 0x01, 1–0 0x02–0x0B, Enter 0x1C, Ctrl 0x1D,
  LShift 0x2A, Alt 0x38, Space 0x39, F1–F10 0x3B–0x44, arrows (E0-prefixed)
  up 0x48, left 0x4B, right 0x4D, down 0x50.
- BIOS INT 16h AH=0/1 reads the BIOS buffer; games with their own handler
  usually do not chain to it.

## Mouse

- INT 33h: AX=0 reset, 3 position/buttons, 7/8 ranges, 0xC handler.
  Positions are in virtual pixels (often 640 wide in mode 13h).

## Sound

- Sound Blaster: base 0x220, DSP reset 0x226, write 0x22C, read 0x22A; IRQ 5
  or 7; 8-bit DMA channel 1, 16-bit 5. BLASTER env variable (`A220 I7 D1 H5
  T6`). Typical sample rates 11025 / 22050 Hz, 8-bit unsigned mono.
- AdLib / OPL2 at 0x388/0x389 (OPL3 also 0x38A/0x38B). The dos-re-suite
  build can log OPL register writes (`DOS_RE_HARNESS_OPL_LOG`).
- Whether a sound plays can depend on the detected device: check the driver's
  device variable before "verifying by ear".

## Formats that recur

- **Smacker** (`SMK2`/`SMK4`): 8-bit palettised video, often with no audio
  track and a separate WAV. The redcat port's `port/src/smacker.c` decodes
  them (all five of that game's films match ffmpeg). [redcat] `ffprobe` gives size,
  frame count and fps.
- **RIFF WAV**, **VOC** (Creative: "Creative Voice File" header, blocks).
- Game archives: usually a count + table of (name, offset) or offsets only;
  check that the table's length matches the index file size exactly.

## Compilers

- **Watcom C 32-bit** (`-3r`/`-5r`): args in EAX, EDX, EBX, ECX, then stack;
  callee preserves the rest. `__FILE__` strings in asserts give source file
  names. `rand()`: seed = seed * 0x41C64E6D + 0x3039, return (seed >> 16) &
  0x7FFF. [redcat: checked against REKEN's code under Unicorn]
- The two below are from common knowledge, not from these projects:
  confirm against the binary's own `rand` before relying on them.
- **Turbo C++ 1990** startup (C0) begins `mov dx,DGROUP; mov cs:[..],dx;
  mov ah,30h; int 21h` and moves the stack into DGROUP: at run time SS = DS,
  not the header's SS. [dave]
- **Borland / Turbo C 16-bit**: cdecl, args on the stack; far calls
  (`lcall`) between segments; `rand()`: seed = seed * 0x015A4E35 + 1,
  return (seed >> 16) & 0x7FFF; `srand(n)` sets the 32-bit seed to n.
  [dave: confirmed in code and against RAM]
- **Microsoft C 16-bit**: cdecl; `rand()`: seed = seed * 214013 + 2531011,
  return (seed >> 16) & 0x7FFF.
- **Turbo Pascal 16-bit**: one code segment per unit, main program first,
  System last, then DS; `55 89 E5` frames; args pushed left to right (the
  first argument is the highest `[bp+n]`). `Random(n)`: RandSeed = RandSeed *
  0x08088405 + 1, return Hi(RandSeed) mod n (same code in 5.x and 6.0).
  Version fingerprint: the heap code. 5.x start-up has `mov ax,es:[2] /
  sub ax,0x1000` (free list 64 KB below the top), 6.0 does not; 5.5's
  free-list allocator calls `HeapError` itself in a retry loop, while the
  older one ends `clc / ret` and `GetMem` tests `HeapError`.
  `tools/tp/rtlcmp.py` against a test build shows it routine by routine.
  TPC.EXE runs headless under stock `dosbox` with `SDL_VIDEODRIVER=dummy`
  (`tools/tp/tpc.sh`). WinWorld's TP 5.0 / 5.5 / 6.0 floppy sets all
  work (TPC.EXE + TURBO.TPL is all a build needs). [SAM2: SAM1.EXE pinned to
  5.0 this way, 0 routines differ; 5.5 differs in 4, 6.0 in 17]
- **Turbo Pascal unit map**: the program entry far-calls every unit's init in
  `uses` order (System first), which is the reverse of the code-segment
  order; a unit without init (Dos) is missing from the chain. `TPC /GD`
  writes a map with segments, publics and, under `{$D+,L+}`, a line-number
  table per unit (the only place private routines show up). A unit whose
  segment starts with a short framed init followed by unframed code is
  Pascal + `{$L}` asm (Crt, most libraries). [SAM2]
- **Turbo Pascal data segment**: typed constants first, then variables, each
  in code-segment order (program, units, System last); System's typed
  constants are a 0x3C-byte block in 5.0 (RandSeed at +0x36), its variables
  start with `Input`, `Output`. Knowing two anchors (RandSeed, Input) splits a
  DS into units. [SAM2]

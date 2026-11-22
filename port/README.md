# port/: conventions for the C reimplementation

## Wacky Wheels: how the port is built (decided 2026-11-21)

METHOD.md section 10: translate, don't rewrite. Here the source already is C, so the compiler does the
translating; what is left is the memory layout, the asm module and the machine.

- **32-bit target only** (`gcc -m32` + SDL2 now, Emscripten wasm32 later). CONFIRMED need: the game's
  data holds 32-bit pointers - 199 pointer fields in `src/ww.c`'s structs and 74 pointer globals - so a
  64-bit build breaks every layout.
- **`src/ww.c` compiles as it is.** Watcom-only syntax (`#pragma aux`, `__interrupt __far`, `<dos.h>`,
  `<conio.h>`, `<i86.h>`) is met by shim headers in `port/include/`, never by editing `src/ww.c`.
- **The data segment stays where the game had it**: object 2 at linear 0x60000-0x93100. At start the
  port reads the player's WW.EXE, applies the LE fixups (the same rule as `tools/le/leparse.py`) and
  maps the object at 0x60000 (`mmap MAP_FIXED`). `src/ww.c`'s extern globals are bound to their game
  addresses by a generated linker script (from `tools/link/symmap.py`). Code addresses stored in data
  (function pointers) are translated to the port's functions through a generated table.
- **The asm module is translated, not run natively.** The game drives the VGA in Mode X (four planes
  behind the sequencer map mask, findings/video.md), and `src/wwasm.asm` has 33 direct 0xA0000
  accesses, 107 `in`/`out` and 23 `cli`/`sti`. Those cannot run in user mode, and plane writes cannot be
  plain memory. `port/tools/asm2c.py` turns the asm into C over emulated registers and flags (no
  self-modifying code; ~40 mnemonics in 14,700 instructions). Every memory operand goes through an
  accessor that routes 0xA0000-0xAFFFF to the emulated VGA. The same output builds for wasm.
- **The machine is emulated, the game is not**: VGA (sequencer map mask, graphics controller write
  modes and latches, DAC 0x3c8/0x3c9, CRTC start address, retrace 0x3da), PIT and the timer ISR,
  keyboard port 0x60 + the game's own `kbd_isr`, `int386` services the game calls, DOS file I/O via the
  host libc. Interrupt handlers the game installs with `_dos_setvect` run at poll points.
- Sound: the task manager (TS_*) runs the game's clocks on the hardware thread. MUSIC_* goes to the
  game's own FM song player, compiled from its rebuild (`src/ass/FMSONG.C`, `FMCHIP.C`; prep.py
  rewrites one NULL read, see there). It writes the OPL2 at 0x388/0x389, which `port/src/audio.c`
  emulates with Nuked OPL3 (`port/third_party/nukedopl`, LGPL) and plays through SDL audio, or with
  `PORT_WAV=file` records in step with the clock (headless); `PORT_NOSOUND=1` silences it. The
  Apogee song plays the original's notes and timing (`port/check.sh` step 3, port/tools/audiocmp.py).
  FX_* is the library's own FX_MAN + MULTIVOC (the 1994 C mixer) over the port's SB16
  (`port/src/blaster.c`): audio.c plays MULTIVOC's DMA ring at 11 kHz, calls MV_ServiceVoc after each
  part as the card's interrupt did, resamples and mixes it with the OPL. DisableInterrupts is a real
  lock (port/include/interrupt.h) that the timer tasks and that interrupt take. The other cards are
  stubs (`port/src/cards.c`). The serial link is not ported.

### Timing (QUEUE Q2)

The game's feel rests on the PC's speed, not only on its clocks. Its menus take Enter from the key
state with no wait for the release, so a key still down when the next menu first polls selects there
too (on the original under DOSBox-X: an 80 ms press selects one menu, 150 ms sometimes two, 1 s
three). Its fades wait for a horizontal blank before each palette entry. Natively both run up to ten
times too fast: a fade took 3 ms, and one Enter went through two menus. Two things put the time back:

- **0x3da from a real clock** with mode 13h's timing (`status1` in `vga.c`): 31.47 kHz lines, 449 a
  frame, display enable off in the last fifth of each line and lines 400-448, retrace on 412-413.
- **Port I/O costs bus time**: each VGA port access spins for DOSBox-X's I/O delay (8-bit 1020 ns,
  16-bit 660 ns, writes 3/4; `io_cost`, `PORT_IO_NS`). A 16-bit `out dx, ax` is one access.

With both, a fade takes 357 ms (original: about 0.3 s), the CLASS-to-LAPS change 111 ms from the key
read, and racetest's key path shows the same screen as the original's at every step
(`port/check.sh`). Access to the VGA memory window is free by default, as in DOSBox-X (`vmemdelay`
off); `PORT_VGA_NS` charges it, as a 1994 ISA card did (~1000 ns a byte). The race's frame rate is
the game's own (about 11 frames a second on track 1, the same with the costs off).

What worked in the earlier ports (redcat: C + SDL2 + Emscripten; cold:
C + SDL2 with a self-check binary).

## Code

- Every function that implements an original routine names it in its
  comment: address, what it does, and the instruction behind any non-obvious
  constant (`/* 0x114b8: the first racer with the most points */`).
- Keep the original's data layout where files are read (structs with the
  offsets in comments); the port reads the player's own game files, it does
  not ship them.
- Original timing: if the game has no frame cap, pick a fixed rate and say
  so; port BIOS-tick delays as whole ticks (`(ms * 10 + 548) / 549` ticks of
  55 ms).
- Films and screens the original shows full-screen in mode 13h stretch to
  640 × 480 (2 × 2.4).
- A debug switch of the original (`/DEBUG`, a level select) is worth
  mirroring as a CLI flag: it is also the way to test.

## Testing without a human

- A headless mode: render N frames with a given input script and write the
  screen buffer as raw bytes or PNG (`tools/real/raw2png.py`), then compare
  with a dump from the original.
- Generators and rules: run the original's routine under Unicorn
  (`tools/le/emu.py`) and compare case by case with the port's, for
  thousands of seeds.
- A self-check binary that exits non-zero on failure (cold's `coldcheck`),
  plus a mutation list: each entry puts one fixed bug back and names the
  check that must then fail. A check that passes with its bug back proves
  nothing.
- For a one-off look at a state that is hard to reach by playing, add a
  temporary hook (an env variable that jumps there), take the screenshot
  under Xvfb, and remove the hook before committing.

### Checks (`port/check.sh`, about 3 minutes)

| Step | Compares with | Mutation that must fail it |
|---|---|---|
| 1 title | `captures/title_ww.png`, pixel-identical | vga.c `plane[(x + 1) & 3]` |
| 2 menus | racetest's ENGINES and overview shots | `PORT_IO_NS=0` |
| 3 sound | `captures/apogee_ww.wav` (audiocmp, >= 0.9) | OPL register + 1 (0.848); 16-bit ring as signed (0.838) |
| 4 race | `captures/race_trace_ww.bin`: 600 race frames, the eight player records byte for byte (port/tools/racecmp.py) | cpu_drive's path step `step < v88c24` (DIFF at frame 35) |

The race is a frame-by-frame trace taken at race_run's `ticks2++`, in the original with
`tools/dosrun.py watch 0x3114c+delta` and in the port with gdb. It works because the race steps
once per frame (ticks pace only the display: the port at 12 and 15 ticks a frame gave the same
600 frames) and because the grid seed (`racetime` at fn_1be3c, which depends on how long the menus
took) is forced to the original run's 459, found by calling the game's own fn_1be3c in the port
under gdb for every seed until its start order matched. No player input: the computer karts race.

## Layout that worked

```
port/
  CMakeLists.txt     desktop (SDL2) and core library targets
  src/               one file per subsystem (race.c, menu.c, smacker.c ...)
  tools/             generators of tables from the original (gen_*.py)
  web/               Emscripten build: shell.html, build.sh, serve.sh
```

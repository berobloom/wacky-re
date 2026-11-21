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
- Sound (src/ass) and the serial link come after the first race runs; until then the sound library
  reports no card.

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

## Layout that worked

```
port/
  CMakeLists.txt     desktop (SDL2) and core library targets
  src/               one file per subsystem (race.c, menu.c, smacker.c ...)
  tools/             generators of tables from the original (gen_*.py)
  web/               Emscripten build: shell.html, build.sh, serve.sh
```

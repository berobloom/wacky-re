# port/: conventions for the C reimplementation

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

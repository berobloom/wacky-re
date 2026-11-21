# Work queue (human-owned)

Work the first item that is neither ticked nor marked READY FOR REVIEW, top to
bottom. While any item here is open it overrides STATUS.md `## Next`.

Rules for the agent:

- **Only the human ticks `[x]`.** When you think an item is done, add
  `READY FOR REVIEW: <check or evidence>` under it, and say in STATUS.md which
  check proves it and which mutation makes that check fail.
- An item is done when its **Accept** line holds, not when a theory explains
  the report. If the Accept line is about the screen, render it and look at
  the image before writing READY FOR REVIEW.
- If new evidence shows an item is wrong or should be split, write that under
  the item. Never delete an item.
- If an item needs the live harness and the harness fails twice in a row,
  write BLOCKED with the reason under it and take the next item that can be
  done offline.

## Items

- [ ] **Q1 Port: first screen from the port**
  Report: the human asked to start the port (2026-11-21); `port/` held only conventions.
  Accept: `port/` builds a 32-bit SDL2 binary from `src/ww.c` unchanged plus the runtime layer, reads the
  player's WW.EXE and data files, runs the game's `main` until the first picture is shown, and a headless
  run writes that screen as a PNG that matches a DOSBox screenshot of the original at the same point.
  READY FOR REVIEW: `port/check.sh` prints `PORTCHECK OK title: SAME mean 0.00 over-40 0 px` - the port's
  title screen (4 s after start, headless) is pixel-identical to captures/title_ww.png, the original
  under DOSBox-X. Mutation that fails it: vga.c `plane[x & 3]` -> `plane[(x + 1) & 3]` (DIFF 12.87%).
  Known gap after it: the Apogee screen ends early, because the silent MUSIC_SongPlaying stub says no
  song is playing (intro's exit test `!MUSIC_SongPlaying() && n >= 8`); it goes with the music port.

- [ ] **Q2 Port: menus to the first race**
  Report: the human asked for the next port milestone (2026-11-21) after Q1.
  Accept: driven headless along tools/link/racetest.sh's key path (Enter x 11 from the title), the port
  shows the ENGINES menu and the first race's track overview, each matching the original's capture
  (captures/engines_menu_ww.png, captures/race_overview_ww.png) within racetest's tolerance (mean RGB
  difference < 20, under 3% of pixels over 40).
  READY FOR REVIEW: `port/check.sh` prints `PORTCHECK OK title SAME; engines: CLOSE mean 0.82 over-40 494 px
  (0.77%); overview: SAME mean 0.00 over-40 0 px (0.00%)` - on racetest's schedule (80 ms presses) the
  port shows ENGINES after the 8th Enter and the overview pixel-identical 8 s after the 11th. Every step
  of the path matches the original's racetest shot (8 of 10 within tolerance; Apogee and the BEAVIS fade
  differ by animation phase). It took the original's timing: the game's FM song player runs (silent), so
  the Apogee screen lasts as long as its song (fixes Q1's known gap); 0x3da follows real scanline timing
  and VGA port I/O costs DOSBox-X's ISA delay, so fades take 357 ms (original ~0.3 s) and a key held
  80 / 150 / 1000 ms on CLASS goes through 1 / 1-2 / 3 menus as on the original (findings/menus.md).
  Mutation that fails it: `io_cost` in vga.c returns at once (engines DIFF mean 109.45, 97.50%: the
  zone select). Known gaps: no OPL2 or sound effects (silent); the race after the overview is not
  compared (racetest's own race shot is timing-dependent).


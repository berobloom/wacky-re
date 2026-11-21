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


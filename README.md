# DREA: DOS reverse-engineering template

A starting point for reverse engineering a DOS game and porting it to C, with
an AI agent (Claude Code or a local model through pi) working next to you.
It works with the patched DOSBox-X and the `dosre` MCP server in
`../dos-re-suite`.

Everything here is generic. It comes from two finished projects:

- **re_cold**: Cold Dreams (1994), 16-bit real mode, reverse engineered by
  long unattended local-model runs over GDB/QMP.
- **redcat**: RedCat's Razende Rekenrace (1996), 32-bit Watcom/DOS/4GW, ported
  to SDL2 and WebAssembly.

Only the parts that would have saved time on a new game came across: the
traps (GOTCHAS.md, harness/TRAPS.md), the working method (METHOD.md), the note-keeping layout,
the harness configuration and the tools with no game in them.

## Start a project

```sh
git clone https://github.com/berobloom/DREA ~/projects/active/<game>-re
cd ~/projects/active/<game>-re && rm -rf .git && git init
./setup.sh            # checks every tool; prints the apt line for what is missing
./setup.sh --suite    # once per machine: fetch and build dos-re-suite
# the game, untouched, in original/
unzip -d original <download>.zip     # or 7z x -ooriginal <image>.iso
(cd original && sha256sum * > SHA256SUMS)
.work/venv/bin/python tools/mz.py original/*.EXE
# the emulator and the agent
GAMEDIR=<DIR> harness/install.sh
.work/venv/bin/python tools/dosrun.py start
cp harness/mcp.json.template .mcp.json   # Claude Code and pi (pi-mcp-adapter reads .mcp.json)
```

`setup.sh` never uses sudo: it prints the install command and you run it, then
run `setup.sh` again until it says `ready`.

Then fill in the `<...>` parts of `AGENTS.md` (`CLAUDE.md` is a link to it),
put your first priorities in `QUEUE.md`, and keep `GOTCHAS.md` growing.
Delete the worked example at the bottom of `findings/_TEMPLATE.md` once the
first real findings exist.

## Layout

| Path | What |
|---|---|
| `AGENTS.md` (`CLAUDE.md`) | project instructions for the agent: session start, evidence rules, where things go |
| `METHOD.md` | how to RE and port one behaviour: trust order, evidence levels, the fix loop, x86-to-C mistakes, checks that can fail |
| `GOTCHAS.md` | the 17 traps to read every session, plus this project's own |
| `harness/TRAPS.md` | all ~60 harness, tooling and process traps by topic, tagged by origin; grep it |
| `QUEUE.md` | the human's work list; only the human ticks items |
| `STATUS.md` | where things stand, `## Next` |
| `QUESTIONS.md` | open questions and hypotheses, numbered |
| `findings/` | facts by subsystem, with evidence levels (`INDEX.md`, `_TEMPLATE.md`) |
| `analysis/` | names and function lists per executable |
| `captures/` | save states and dumps that checks use (`MANIFEST.md`) |
| `reference/DOS.md` | platform facts: memory map, PSP/MZ/LE, VGA, PIT, keyboard, Sound Blaster, compilers' rand() |
| `setup.sh` | checks and sets up the tools; `--suite` builds the emulator |
| `harness/` | DOSBox-X config template, install script, MCP configs, `MCP-GUIDE.md` |
| `tools/` | `mz.py`, `dosrun.py`, real-mode and LE tools, Ghidra headless (`tools/README.md`) |
| `port/README.md` | conventions for the C port and for testing it without a human |
| `original/` | the untouched game files (not committed) |
| `.work/` | everything derived (not committed) |

## Not included

- **re_cold's overnight loop** (`overnight/`: the pi supervisor, the watchdog
  extension, `check.sh` with build + self-check + mutation gates). It is tied
  to that project's check binary and is not committed there either. Copy it
  and adapt `check.sh` and `prompt.md` when a project has a port with a
  self-check to gate on.
- **The MCP server and DOSBox-X build**: they stay in `dos-re-suite`
  (GitHub `berobloom/dos-re-suite`, private); its README is the manual.
- **Game-specific decoders** (Davilex IND/IMG archives, Cold Dreams sprite
  banks). The redcat port's `smacker.c` is a general Smacker decoder worth
  copying when a game has `.SMK` films.

## Feeding lessons back

A project finds traps and methods that are not about its game. Tag them
`[generic]` in the project's `GOTCHAS.md` (or a finding's `## Traps`), and
from time to time copy them here:

- harness and tool traps → `harness/TRAPS.md` (and `GOTCHAS.md` if one would
  have saved a whole session), with the project's tag;
- platform facts confirmed in a game → `reference/DOS.md`, with the game named;
- a reusable tool → `tools/`, stripped of game addresses, with a usage line;
- a way of working → `METHOD.md`.

Keep `GOTCHAS.md` at one screen: when it grows, move the least costly entry to
`TRAPS.md`.

## License

Copyright (C) 2026 Berobloom

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It comes WITHOUT ANY WARRANTY. The full text is in [LICENSE](LICENSE).

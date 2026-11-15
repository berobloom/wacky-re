# harness/: running the original

The emulator and the MCP server are not copied into each project. They live
in `/home/bjorn/projects/active/dos-re-suite`: a patched DOSBox-X
(`backend/build.sh`, GDB stub + QMP, linear-address breakpoint fix, the
in-CPU input-script hook, OPL logging) and `mcp/server.py` (the `dosre`
tools). Build it once; see its README.

GitHub: `berobloom/dos-re-suite` (private). Since 2026-10-31 it carries
re_cold's newer server (35 tools: `type_text`, `read_screen_text`,
`capture_wave`, resolved PC) with configurable ports (`DOS_RE_GDB_PORT`,
`DOS_RE_QMP_PORT`); `start_dosbox` no longer refuses because another
project's emulator runs. Its README is the manual.

## Files

| File | Use |
|---|---|
| `game.conf.in` | DOSBox-X config template; `install.sh` fills in the paths |
| `install.sh` | copies `original/` to `.work/dos/C/<GAMEDIR>`, writes `.work/dos/game.conf` |
| `mcp.json.template` | the `dosre` server for Claude Code (`.mcp.json` in the project root) |
| `MCP-GUIDE.md` | how to work through the MCP tools: address rules, the run/halt cycle, first minutes |
| `TRAPS.md` | every known harness, tooling and process trap, by topic (grep it) |

## Two ways to drive the emulator

- **MCP (`dosre`)**: for an agent: breakpoints, stepping, registers, memory,
  keys, screenshots, save states. Read `MCP-GUIDE.md` first.
- **`tools/dosrun.py`**: a small CLI over the same QMP/GDB ports for scripts:
  `start` (hidden Xvfb display), `type`, `key` (real down/up events), `shot`,
  `mem`, `find`, `poke`, `regs`, `save`/`load`, `locate` (LE run-time deltas),
  `stop` (SIGKILL). Use one or the other at a time: one client per port.

## First boot checklist

1. `GAMEDIR=<DIR> harness/install.sh`; edit `.work/dos/game.conf` if the game
   needs a CD drive, a sound card or a VESA adapter.
2. `tools/dosrun.py start`, `tools/dosrun.py type '<GAME>'`,
   `tools/dosrun.py shot /tmp/x.png`. Text mode: read B8000 instead.
3. Record in `findings/memory.md`: the boot's PSP/CS/DS (real mode) or the LE
   deltas (`dosrun.py locate <prog>`), with the exact `.conf` and launch line.
4. `save` a state at the first interesting screen; list it in
   `captures/MANIFEST.md`.

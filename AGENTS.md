# Wacky Wheels reverse engineering

## Project goal

Reverse engineer the 1994 DOS game Wacky Wheels (Beavis Soft, published by Apogee; version 1.1,
registered), rebuild WW.EXE's C byte for byte (`src/ww.c`, MATCHING.md), and run that source on
the desktop (SDL2) through the port's runtime (`port/`).

## Session start (in this order)

1. `git status --short`: an interrupted run may have left work behind.
2. `QUEUE.md`: the human's work list. Its first open item is the task.
3. `METHOD.md`: how to RE and port one thing. Follow it.
4. `findings/INDEX.md`, then only the one findings file the task needs.
5. `STATUS.md`, then `GOTCHAS.md` (the top list first; the rest is in
   `harness/TRAPS.md`: grep it when something looks impossible).

## Context budget

Written for small local models as much as for large ones:

- Grep before reading; read a range, not a file. Never read whole:
  decompiler output (`.work/ghidra/*.c`), call graphs, the port's main
  source, `harness/TRAPS.md`, the MCP server.
- Cap every command's output (`| head -80`); every scan loop advances its
  index and has a limit.
- Large tool results are written to files: parse the file, do not call the
  tool again to see it.
- If the same call fails twice with the same message, change the approach or
  mark the item BLOCKED and continue offline. If the harness fails twice in a
  row, stop using it for this item.

## Evidence rules

Never present an inference as fact. Every finding carries a level:
CONFIRMED, STRONGLY SUPPORTED, LIKELY or SPECULATIVE (METHOD.md section 2),
plus its evidence (addresses, capture files, the experiment). Prefer evidence
from the binary and runtime experiments over external documentation.

## Where things go

| What | Where |
|---|---|
| Facts about the game, by subsystem | `findings/<subsystem>.md` (index: `findings/INDEX.md`) |
| Open questions and hypotheses | `QUESTIONS.md` |
| Function / variable names | `analysis/<prog>_names.txt` (`0xADDR name  # LEVEL: why`) |
| Harness and tool traps | `GOTCHAS.md` "This project's traps" (game traps: the findings file's `## Traps`); tag ones that are not about this game `[generic]` |
| Captures, states, dumps the checks use | `captures/` + `captures/MANIFEST.md` |
| What was done, what is next | `STATUS.md` |
| The human's priorities | `QUEUE.md` (only the human ticks `[x]`) |

Do not duplicate a finding; link to it.

## Feeding DREA

Every problem this project runs into that is not about the game itself (a
harness or tool trap, a wrong doc, a platform fact, a better method) is also
written into `../DREA` the same session, tagged with this project's name:
`harness/TRAPS.md` (and `GOTCHAS.md` if it cost a session), `reference/DOS.md`,
`METHOD.md`, or a fix to the tool or doc that was wrong. Say so in the commit.

## Binaries and addresses

- `original/` holds the untouched game files (read-only; checksums in
  `original/SHA256SUMS`). Derived files go to `.work/` (not committed).
- Executables: `WW.EXE` (the game) and `SETUP.EXE`, both MZ stubs with DOS/4GW bound in front of
  an LE program (Watcom C/C++32 9.5b); details in `findings/executables.md`.
- Address convention: the LE image's own addresses (code object at 0x10000), as
  `tools/le/leparse.py` loads them. Run-time deltas this boot:
  derive them (`tools/dosrun.py locate <prog>` for LE), never copy them from
  notes.

## Running the original

The patched DOSBox-X and MCP server live in `../dos-re-suite`
(`harness/README.md`). `harness/install.sh` installs the game into
`.work/dos/C`; `tools/dosrun.py` drives it on a hidden Xvfb display (type,
keys, screenshots, memory, save states). Through MCP: the `dosre` server
(`.mcp.json`, read by Claude Code and by pi-mcp-adapter). One client per
port: never run two agents on
one harness.

Debug switches of the game: none known.

## Reverse engineering rules

- Preserve the original binaries.
- Record addresses for every important conclusion.
- Small experiments that distinguish hypotheses beat long traces.
- When you discover something important, update the findings file in the
  same commit as the code that uses it.

## Commits

Commit after each finished function (MATCH or near-miss decision), not at the
end of a session; also when asked. Use explicit paths. Short subjects in the
project's style (`Port: ...`, `Findings: ...`).

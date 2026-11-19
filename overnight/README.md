# Overnight self-healing RE loop

Ralph-style loop: fresh pi context per pass, all state in files and git. Same supervisor as the
SAM project's loop (see `../../SAM/overnight/README.md` for the full failure/remedy table);
this copy is wired to Wacky Wheels.

```
overnight/run.sh                        # a night: new branch overnight/YYYYMMDD, sleep inhibited
overnight/run.sh --hours 3              # shorter; --passes N caps passes
python3 overnight/overnight.py --smoke  # ~1 min: model, tool calls, watchdog, policy block
python3 overnight/overnight.py --dry-run
touch overnight/STOP                    # stop after the current pass; kill <pid> = stop now
python3 overnight/watch.py              # live view of the newest run
```
Morning: `overnight/runs/<stamp>/REPORT.md`, then review `git log --stat main..HEAD` and merge.

| File | Role |
|---|---|
| `overnight.py` | supervisor: passes, gates, resumes, rollback, report |
| `watch.py` | read-only live viewer |
| `watchdog.ts` | pi extension: in-session healing (policy blocks, nudges, turn budget) |
| `prompt.md` | the one-iteration procedure (follows AGENTS.md) |
| `check.sh [rev]` | health gate on a clean worktree: original checksums, tools compile, `src/check.sh` MATCH count >= `src/matched.baseline`, STATUS bounds |
| `cleanup.sh` | kills only this project's dosbox-x (`.work/dos/game.conf`), dosrun.py, Xvfb :57 |
| `config.json` | model, budgets, policy (this project's harness ports: gdb 2161 / QMP 4446; 2159/4444/2160/4445 belong to the other projects and are forbidden) |

`src/matched.baseline` rises with the matching rebuild; check.sh fails if matched functions drop.
Set the harness ports to 2161/4446 in `.work/dos/game.conf` (dosrun.py reads them from there) and
in the MCP env (`DOS_RE_GDB_PORT`/`DOS_RE_QMP_PORT`) when the harness gets installed.

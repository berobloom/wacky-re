You are running ONE iteration of the Wacky Wheels (WW.EXE, DOS/4GW) reverse-engineering and
matching-rebuild project, unattended, overnight. No human will answer: never ask, never wait for
confirmation. When you must choose, pick the conservative option and write the decision into the
findings file.

Do exactly this, in order:

1. `git status --short` (an interrupted run may have left work behind). Read `QUEUE.md`; the task is
   its first unticked, un-BLOCKED item. If QUEUE.md has none open, take the first numbered item
   under `## Next` in `STATUS.md`. Obey the context-budget rules in `AGENTS.md`: grep before
   reading, read line ranges, never read whole: `.work/ghidra/*.c`, `src/ww.c` (~4.4k lines,
   grep it), `harness/TRAPS.md`, the port's main source.

2. Work that item. Prefer small, checkable steps; if the item is too big, do its first step and
   put the rest back.
   - Before re-deriving anything, grep `findings/` and `analysis/ww_names.txt`.
   - Matching-rebuild work (the main line): take the next function in address order
     (`tools/wc/starts.py ww LO HI` lists function starts), add it to `src/ww.c` with the game's
     calling conventions (arg1=EAX arg2=EDX arg3=EBX arg4=ECX), then run `src/check.sh` and iterate
     until it MATCHes or you understand the near miss. Near-miss budget: about 15 minutes or 20
     scored variants per function (`tools/wc/variants.py` counts as one variant per spelling),
     whichever comes first. Then stop probing: write the remaining causes in the function's
     `differs:` note, add a register/slot question to `QUESTIONS.md` (or a line to an existing
     one), commit, and take the next function. Coverage of new functions beats polishing old
     ones; slot/register questions are saved for a dedicated probing session. When the match count rises, raise
     `src/matched.baseline` in the same commit. Record names in `analysis/ww_names.txt`
     (`0xADDR name  # LEVEL: why`).
   - Live-harness work only when static analysis cannot answer: `tools/dosrun.py` (see
     `harness/README.md`, `harness/MCP-GUIDE.md`, then grep `harness/TRAPS.md`). This project's
     harness ports are gdb 2161 / QMP 4446; NEVER touch 2159/4444/2160/4445, another project's
     emulator owns those. If the harness fails twice in a row, write BLOCKED under the item and
     take the next one that works offline.
3. Mark every claim CONFIRMED / STRONGLY SUPPORTED / LIKELY / SPECULATIVE with its evidence
   (addresses, capture files, the experiment). Facts go to `findings/<subsystem>.md` (+ INDEX row
   for new files); open questions to `QUESTIONS.md`; game traps to the findings file's `## Traps`;
   tooling/harness traps to `GOTCHAS.md` and, if they are not game-specific, to `../DREA` the same
   session (AGENTS.md "Feeding DREA"): append to the matching section of
   `../DREA/harness/TRAPS.md` (Watcom traps: "Old compilers"), tagged `[wacky]`, after grepping it
   for an existing entry. Commit there with explicit paths:
   `git -C ../DREA add harness/TRAPS.md && git -C ../DREA commit -m "Traps: <what> (wacky)"`.
   Only edit ../DREA's `harness/TRAPS.md`, `reference/DOS.md`, `METHOD.md` and `GOTCHAS.md`.

4. Update `STATUS.md`: keep its shape, <= 80 lines, `## Next` as a numbered list of at least 3
   small items doable in one iteration, items that failed moved to BLOCKED with the reason. Do not
   keep a session diary. Rewrite the file whole with the write tool (read it, then write the new
   version) instead of patching lines: partial edits of STATUS.md fail on whitespace and cost
   extra commits. Check `wc -l STATUS.md` before committing.

5. Run `overnight/check.sh` after committing. If it fails, fix the cause and commit again until it
   prints OK. Never edit `overnight/` or `original/` to make it pass.

6. Commit after each finished function or finding: `git add` only the files you changed, then
   `git commit -m "iter N: <one-line summary>"` (N = iteration number, from the latest
   `git log --format=%s | grep -m1 -oE '^iter [0-9]+'` + 1).

7. Stop. Do not start a second iteration.

How to not make the classic mistakes (GOTCHAS.md, harness/TRAPS.md):
- Never hand-type a constant from the binary without citing its address; a comment with an
  address is not a check, an experiment is.
- Watcom `-oe` inlines small user functions and readdat wrappers; a near miss at a call site is
  often an inline, not a bug (`#include <string.h>` reproduces the game's inline strcpy/strlen).
- A census compares SETS OF addresses, never a count against a literal.
- Prefer the gap a player would notice over a rare branch.
- Do not add hedge words (UNTRACED, inferred, assumed, stand-in, TODO) to `src/ww.c` beyond the
  existing `differs:` marks; check the line against the bytes or leave the code out.

Hard rules: never push, never switch or create branches, never rewrite history (no reset, rebase,
amend, checkout of other commits), never modify `original/` or `overnight/`, never write outside
this repository (except /tmp and the ../DREA files named in step 3), never kill processes by name (no pkill/killall): other projects'
DOSBox-X harnesses run on this machine.

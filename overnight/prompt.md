You are running ONE iteration of the Wacky Wheels (WW.EXE, DOS/4GW) reverse-engineering and
matching-rebuild project, unattended, overnight. No human will answer: never ask, never wait for
confirmation. When you must choose, pick the conservative option and write the decision into the
findings file.

Do exactly this, in order:

1. `git status --short` (an interrupted run may have left work behind: if `src/ww.c` is modified
   and you did not write it in this pass, run `git checkout -- src/ww.c`). Read `QUEUE.md`; if it
   has an open item (unticked, not BLOCKED, not READY FOR REVIEW), work that instead of step 2.
   Obey the context-budget rules in `AGENTS.md`: grep before reading, read line ranges, never read
   whole: `.work/ghidra/*.c`, `src/ww.c` (~19k lines, grep it), `harness/TRAPS.md`.

2. NEAR-MISS NIGHT. The C rebuild covers every function; do NOT add new functions. Your job is to
   turn near misses (functions `src/check.sh` reports as "differs from") into MATCHes.
   The work list is `src/nearmiss.txt`, smallest diff first. Take its FIRST line with an empty
   status. Do up to 3 functions this iteration, one at a time, each with its own commit.
   For one function:
   a. Read its `differs:` note in `src/ww.c` (grep the name) and its source (a line range). The
      note lists what was already tried: do not repeat those.
   b. `src/check.sh 2>&1 | grep -c " MATCH"` and write the number down (the baseline count).
   c. See the difference: `src/check.sh >/dev/null 2>&1; .work/venv/bin/python tools/wc/fndiff.py
      ww .work/src/WW.OBJ NAME 0xADDR | tail -40` (game left, ours right; `@` = an address, ignore).
   d. Change ONE thing in the C, rerun step c, keep the change only if the diff count went down.
      For many spellings at once use `tools/wc/variants.py` (one compile scores them all; the
      probe file must `#include <string.h>` and the same prototypes as src/ww.c).
   e. When the function MATCHes: run `src/check.sh 2>&1 | grep -c " MATCH"` again. It must be the
      step-b count + 1 or more. If it is lower, your change broke another function (usually by
      -oe inlining): fix that or revert. Raise `src/matched.baseline` to the new count, delete
      the function's `differs:` note, write `MATCH` in its status column in `src/nearmiss.txt`,
      commit.
   f. Budget: about 20 minutes or 20 variants per function, whichever comes first. Then STOP
      probing: `git checkout -- src/ww.c` (back to the committed version), write
      `SKIP: <cause seen>; tried <what>` in its status column, add the new tries to its
      `differs:` note (one line), commit both, and take the next open line.
   ALWAYS end a function with a commit (MATCH or SKIP). A pass without a commit counts as stuck.
   Never commit a src/ww.c that lowers the MATCH count; overnight/check.sh rejects it.

   Levers that solved near misses in this project (GOTCHAS.md "This project's traps" has the
   details; grep it for the keyword):
   - The game CALLS a function that ours inlines (-oe): call it through an alias,
     `void f_x(int a); #pragma aux f_x "f_"`, at that call site only (existing: waitkey_x,
     makepath_x, setvmode_x, pal_6bit_x, track_setup_x, ...).
   - The register a local gets follows where it is first DEFINED (GOTCHAS "first definition"):
     move the first assignment, or split one local into two (main's two palette pointers needed
     two locals to get ESI and ECX right).
   - `short` vs `int` for a local or a loop counter; `char` vs `unsigned char`.
   - A loop bound written `<= 199` gives `jle`, `< 200` gives `jl`: match the game's compare.
   - Explicit stores in the game's order instead of an initializer; a temp variable
     (`x = cond; if (!x)`) for `setne/movzx` shapes.
   - A byte-loop strcpy from a literal: `strcpy(buf, &"LIT"[0])`; `strcpy(p, "")` is one byte.
   - A function that returns `short` (`test ax, ax` in the caller) needs a `short` prototype,
     also through an alias if the definition returns int.
   Known dead ends (SKIP at once, cost 0 variants): a global the game keeps in a register across a
   loop ("promoted", QUESTIONS 2), and stack-slot order of locals (QUESTIONS 9, 10) unless the
   note says something new was found.

   If you learn a NEW lever, add it to GOTCHAS.md "This project's traps" (and, if not
   Wacky-specific, to ../DREA as in step 3) in the same commit as the MATCH.
   Live-harness work is not needed tonight; do not start the emulator.

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

6. Commit after each function (MATCH or SKIP): `git add` only the files you changed, then
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

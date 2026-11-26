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

2. THE TASK comes from `QUEUE.md` only (step 1). If it has no open item, write nothing, commit
   nothing, print `NO TASK: QUEUE.md has no open item` and stop: the night's list of register-home
   functions is used up (iter 104: each is `MATCH` or `VB:`), and nights that steered themselves from
   STATUS `## Next` repeated themselves (2026-11-19). When the item names near-miss functions, work
   them with the recipe below, up to 3 per iteration, and skip one whose `src/nearmiss.txt` status
   already starts with `VB:`. Levers for register-home misses: the game's register is often Watcom's
   own temporary for a GLOBAL the source used in place (`v88cf0 = 0; if (k) v88cf0 += 2;`, not a
   local copied into it: iter 93s, findings/compiler.md, GOTCHAS "Which of two locals gets EAX").
   For one function:
   a. Read its `differs:` note in `src/ww.c` (grep `^[a-z].* NAME(` and read from the comment above
      it to the closing brace) and the rows: `.work/venv/bin/python tools/wc/fndiff.py ww
      .work/src/WW.OBJ NAME 0xADDR | head -40` (run `src/check.sh >/dev/null 2>&1` once first if
      .work/src/WW.OBJ is missing). Name which local or global each differing register holds.
   b. Write up to 12 variants into `.work/vbatch/NAME.py`, none already in the differs note:
      `VARIANTS = [('label', [('old text', 'new text'), ...]), ...]`. Each old text must occur
      exactly once in src/ww.c: include the function's own lines (a statement plus its neighbour),
      never a bare `c = 0;`. Use triple-quoted Python strings for multi-line text and keep the
      file's 4-space indentation exactly. Levers, best first:
      1. delete a local and use the global it copies from or into in place, in every statement;
      2. the reverse: cache a global the function reads several times in a new local;
      3. a pointer local <-> the indexed global array expression (`p->f` <-> `arr[i].f`);
      4. merge two locals that are never live together, or split one local used for two jobs;
      5. a `for` <-> `while`/`do` loop, `x += k` <-> `x = x + k`, an `if/else` <-> `?:`;
      6. move a local's declaration or first assignment (only when it changes the code order).
   c. Run `.work/venv/bin/python tools/wcc95/vbatch.py src/ww.c NAME 0xADDR .work/vbatch/NAME.py`
      (35 seconds a variant; it compiles the whole file in `.work/vbatch/` and never touches
      src/ww.c; `control` must show the row count from step a). One vbatch at a time. A variant
      that prints `pattern not found once`: fix its old text and rerun it alone afterwards.
   d. A variant with 0 instruction diffs: apply exactly that change to src/ww.c, replace the
      `differs:` part of its comment with one line saying what the source shape is (and `iter N`),
      run `src/check.sh 2>&1 | grep -c " MATCH"`: it must be >= `src/matched.baseline` + 1; then
      write the new count into `src/matched.baseline`, set the nearmiss.txt status to `MATCH (iter N:
      <shape>) Was: <old status>`, run `.work/venv/bin/python tools/progress.py`, commit (src/ww.c,
      src/matched.baseline, src/nearmiss.txt, PROGRESS.md, docs/WWPROGRESS.*). If a lever worked,
      add one line to GOTCHAS "Which of two locals gets EAX" naming the function.
      A variant with fewer rows but not 0: apply it only if the whole check's MATCH count stays the
      same and the function's row count drops by 2 or more; say so in the differs note.
      No variant helps: prepend `VB: iter N, K variants (labels), best R rows (label)` to the
      nearmiss.txt status and add the labels to the differs note's tried list. Commit that.
   e. Budget: about 25 minutes per function. ALWAYS end a function with a commit. A pass without a
      commit counts as stuck. Never commit a src/ww.c that lowers the MATCH count.
   Not tonight: `tools/wcc95/ratrace.py`, `vtrace.py`, `setup.sh` (they run a DOSBox-X harness), the
   emulator in any other form, `tools/wc/difffn.py`, new functions. `tools/wc/wcc.sh` (used by
   vbatch.py and src/check.sh) runs a headless stock DOSBox and is allowed. Never name `dosrun.py`
   in a shell command: `overnight/cleanup.sh` kills any process whose command line contains it.

3. Mark every claim CONFIRMED / STRONGLY SUPPORTED / LIKELY / SPECULATIVE with its evidence
   (addresses, capture files, the experiment). Facts go to `findings/<subsystem>.md` (+ INDEX row
   for new files); open questions to `QUESTIONS.md`; game traps to the findings file's `## Traps`;
   tooling/harness traps to `GOTCHAS.md` and, if they are not game-specific, to `../DREA` the same
   session (AGENTS.md "Feeding DREA"): append to the matching section of
   `../DREA/harness/TRAPS.md` (Watcom traps: "Old compilers"), tagged `[wacky]`, after grepping it
   for an existing entry. Commit there with explicit paths:
   `git -C ../DREA add harness/TRAPS.md && git -C ../DREA commit -m "Traps: <what> (wacky)"`.
   Only edit ../DREA's `harness/TRAPS.md`, `reference/DOS.md`, `METHOD.md` and `GOTCHAS.md`.

4. Update `STATUS.md` ONCE, at the end of the iteration, inside your last commit (not a commit of
   its own). Change only the match count line and the `## Next` items (at least 3, small, doable in
   one iteration). Levers, tried spellings and SKIP lists do NOT go into STATUS.md: they live in
   GOTCHAS.md, the `differs:` notes and `src/nearmiss.txt`. Keep it at 80 lines or fewer (`wc -l
   STATUS.md`; `overnight/check.sh` fails above 80); if it is longer, delete your own additions rather than trimming in extra commits.

5. Run `overnight/check.sh` after committing. If it fails, fix the cause and commit again until it
   prints OK. Never edit `overnight/` or `original/` to make it pass.

6. Commit after each function: `git add` only the files you changed, then
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

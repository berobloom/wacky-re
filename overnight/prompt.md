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

2. REGISTER-HOME NIGHT: the near misses whose only difference is which register a value lives in.
   210 of 285 functions are byte-identical (`src/matched.baseline`). Iter 93s matched netkeys_out,
   netkeys_pack and netkeys_in after 30+ failed spellings, by one idea: the game had NO local there.
   The game's register was Watcom's own temporary for a GLOBAL that the source used in place
   (`v88cf0 = 0; if (k) v88cf0 += 2;` instead of `c = 0; if (k) c += 2; v88cf0 = c;`). A register
   the allocator would never give a declared local is often the home of such a compiler temp
   (findings/compiler.md, GOTCHAS "Which of two locals gets EAX"). Tonight retries that idea and its
   relatives on these functions, in this order, up to 3 per iteration:
     shots_clear 0x2d4fc, shots_move 0x2602c, voices_reset 0x11adc, drawsprite_clip 0x11404,
     self_project 0x26a3c, fn_1bf00 0x1bf00, net_input 0x2f4a8, com_wait_byte 0x2df70,
     com_expect 0x2e008, fade_in 0x32cf8, view_at_curpos 0x2f408, pcx_load 0x27060,
     net_recv 0x2db00, fn_19db8 0x19db8, car_laps 0x12b44
   Skip one whose `src/nearmiss.txt` status already starts with `VB:` (an earlier pass did it).
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
   GOTCHAS.md, the `differs:` notes and `src/nearmiss.txt`. Keep it under 60 lines (`wc -l
   STATUS.md`); if it is longer, delete your own additions rather than trimming in extra commits.

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

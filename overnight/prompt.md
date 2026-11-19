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

2. BEHAVIOUR NIGHT: THE CAPPED FUNCTIONS. The C rebuild covers every function (207 of 285
   byte-identical); do NOT add functions and do NOT chase row counts tonight. `tools/wc/difffn.py`
   runs one function in WW.EXE and in the hybrid build under Unicorn and compares the data segment,
   EAX and the library calls. It says SAME for 74 of the 78 open near misses, but 28 of those runs
   stop at a cap before the function finishes, so their SAME covers only part of the code:
   - 3M-instruction cap ("both stopped at the 3000000-instruction cap"): classes_menu com_wait_byte
     com_expect com_open kart_draw intro credits_screen showlines race_records_menu engines_menu
     volume_menu main_menu fn_193a8 chat race_results menu cars_menu fn_1832c
   - 300-call cap ("CALLCAP 300/300"): screenshot fn_17928 demo_mode fn_1be3c fn_1c0f4 enter_name
     fn_1d704 main trial_book race_run
   Take them in this order, up to 3 per iteration. Skip one whose `src/nearmiss.txt` line already has
   `SEEDED:` (an earlier pass did it). For one function:
   a. Prepare once per iteration: `src/check.sh >/dev/null 2>&1; tools/link/build.sh >/dev/null 2>&1;
      .work/venv/bin/python tools/le/leparse.py .work/link/WWNEW.EXE .work/le/wwnew >/dev/null`.
   b. Find the loop it never leaves: `timeout 300 .work/venv/bin/python tools/wc/difffn.py NAME` and
      `python3 tools/le/disasm.py ww 0xADDR 400 | grep -n -E 'cmp|test|j[a-z]+ '` (chunk it: the
      tool stops at the first undecodable byte). A wait loop jumps back to an earlier address and
      tests a global (`cmp byte ptr [0x88ccd], 0`, a key flag, a timer, a menu result) that only an
      interrupt or a stubbed call would change. Note that global and the value that exits the loop.
   c. Seed it: `SEED=0xADDR=VALUE[:b|:w|:d] timeout 300 .work/venv/bin/python tools/wc/difffn.py NAME`
      (several: comma-separated; width after a colon, default a dword). Raise `COUNT=`/`CALLS=` only
      if the run is still making progress. First run the control with the same SEED:
      `NEW=wworig MAP=.work/link/WWORIG.MAP SEED=... difffn.py NAME` must print SAME; if the control
      is not SAME, the seed is wrong, not our C.
   d. Verdict. Both sides return (no cap, no CRASH) and SAME: done. A DIFF: read the watch lines
      (the instruction that writes each differing byte, both sides), find that place in our C, and
      decide BUG (our C computes something else: fix it, citing the game's address in a comment) or
      SAME-shape (a stack address cached in a global, a dead pointer: findings/game-loop.md lists the
      known ones; say which). If no seed gets both sides out of the loop within the budget, that is
      the verdict too: `SEEDED: still capped (<global tried>, <why>)`.
   e. Write `SEEDED: <seed used> -> <SAME | n bugs fixed (what) | DIFF same-shape (what) | still
      capped (why)>` at the start of the status column in `src/nearmiss.txt` (keep the old text after
      it). A bug you could not fix: `SEEDED: OPEN <what, game address>` and add it to QUESTIONS.md.
   f. After a C change: `src/check.sh 2>&1 | grep -c " MATCH"` must be >= `src/matched.baseline`
      (if a fix makes another function stop matching, undo it and write the BUG down as unfixed), then
      `.work/venv/bin/python tools/link/fixcensus.py` must print `0 unexplained`, then the behaviour
      check below.
   g. Budget: about 25 minutes per function. A bug fix is worth more than any verdict.
   ALWAYS end a function with a commit. A pass without a commit counts as stuck.
   Never commit a src/ww.c that lowers the MATCH count; overnight/check.sh rejects it.
   Do not guess: a DIFF you cannot place in our C with the game's instruction in front of you is
   `DIFF unknown`, written down as such, not "fixed".
   Behaviour check, once per function after a BUG fix (not for SAME-only functions):
      `tools/link/racetest.sh` (~4 minutes; builds the hybrid WW.EXE from our C and the game's
      library and data, boots it in the harness, drives the menus to the first race and compares the
      screens with the original's). It prints one line: `RACETEST OK`, `RACETEST FAIL`,
      `RACETEST NOBUILD` or `RACETEST NOHARNESS`. Put that line in the function's commit message.
      The first race's start grid is drawn at random (findings/game-loop.md `## Traps`): a race
      line under 3% bad is OK; a FAIL on the race screen alone: look at `.work/link/racetest_race.png`
      before blaming your fix. NOBUILD after your change means your C broke the link: read
      `.work/link/racetest.build` and fix it. NOHARNESS: note it in the commit and do not retry this
      iteration.
   The emulator is used ONLY through `tools/link/racetest.sh`: no `tools/dosrun.py`, no MCP dosre
   tools, no DOSBox commands of your own, never two racetests at once. Never name `dosrun.py` in a
   shell command at all: `overnight/cleanup.sh` kills any process whose command line contains it,
   your own shell included (GOTCHAS).

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

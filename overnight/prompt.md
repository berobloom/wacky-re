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

2. BEHAVIOUR NIGHT. The C rebuild covers every function and every near miss is MATCH, SKIP or
   LAYOUT; do NOT add functions and do NOT chase row counts tonight. A near miss can still do the
   wrong thing: the hybrid link (tools/link, findings/executables.md) crashed in fn_1832c, which
   read 0x300 bytes into a 20-byte stack array and loaded 4 cards where the game loads 3, and
   fn_16eac read field +0x20 where the game reads +2. Row counts hid both. Your job: find and fix
   such behaviour differences. Do up to 3 functions this iteration, one at a time, each with its
   own commit.

   The work list: `.work/venv/bin/python tools/wc/nmrefs.py ww .work/src/WW.OBJ > /tmp/nmrefs.txt`
   (after `src/check.sh >/dev/null 2>&1`), then every function with `DIFFER` or a `consts` line, in
   file order. Skip a function whose `src/nearmiss.txt` status already has `BEHAV:` (done by an
   earlier pass). For one function:
   a. `.work/venv/bin/python tools/wc/nmrefs.py ww .work/src/WW.OBJ -v NAME=0xADDR` lists what only
      one side uses: `game only` addresses (a global, a call, a string the game uses and we do
      not), `consts ... game only` (a loop bound, a size, an enum value, a struct field `+0x..`).
   b. For each item, find the game's instruction: `python3 tools/le/disasm.py ww 0xADDR N | grep`
      for the value, and read 10-20 lines around it. Find the matching place in our C (grep the
      function, read a line range) and decide, writing one line per item in the function's
      `differs:` note:
      - BUG: our C computes something else (another bound, another field, another buffer, a call
        or a branch the game has and we dropped). Fix the C to do what the game does, citing the
        game's address in a comment (`/* 0x18664 cmp card_n,3 */`).
      - SAME: the same behaviour in another shape (the game keeps the value in a register, the
        `sub esp` frame size, an inlined helper, a jump table). Say which.
   c. After the fix: `src/check.sh 2>&1 | grep -c " MATCH"` must be >= `src/matched.baseline`
      (never lower; if a fix makes another function stop matching, inline effects, undo it and
      write the BUG down as unfixed). The function's own row count MAY go up: correctness first.
      Re-run nmrefs for the function: the fixed items must be gone.
   d. Write `BEHAV: <n> bugs fixed (<what>), <m> same-shape` at the start of its status column in
      `src/nearmiss.txt` (keep the old status text after it, and update the row count in column 1
      from fndiff), commit. A bug you could not fix: `BEHAV: OPEN <what, game address>` and add it
      to QUESTIONS.md.
   e. Budget: about 25 minutes per function. A bug fix is worth more than any row gain.
   ALWAYS end a function with a commit. A pass without a commit counts as stuck.
   Never commit a src/ww.c that lowers the MATCH count; overnight/check.sh rejects it.
   Do not guess: an item you cannot place in our C with the game's instruction in front of you is
   SAME-unknown, written down as such, not "fixed".
   f. Behaviour check, once per function after a BUG fix (not for SAME-only functions):
      `tools/link/racetest.sh` (~4 minutes; builds the hybrid WW.EXE from our C and the game's
      library and data, boots it in the harness, drives the menus to the first race's overview and
      compares the screen with the original's). It prints one line: `RACETEST OK`, `RACETEST FAIL`,
      `RACETEST NOBUILD` or `RACETEST NOHARNESS`. Put that line in the function's commit message.
      Read STATUS.md `## Next` for the known state: a FAIL that STATUS already explains is not
      caused by your fix; an OK where STATUS says FAIL is news - write it into STATUS.md. NOBUILD
      after your change means your C broke the link: read `.work/link/racetest.build` and fix it.
      NOHARNESS: note it in the commit and do not retry this iteration.
   The emulator is used ONLY through `tools/link/racetest.sh`: no `tools/dosrun.py`, no MCP dosre
   tools, no DOSBox commands of your own, never two racetests at once.

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

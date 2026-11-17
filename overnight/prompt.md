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
      ww .work/src/WW.OBJ NAME 0xADDR | tail -40` (OURS LEFT, game right; `@` = an address). The
      count already leaves out the masked-fixup `[reg - @]`/`[reg + @]` pairs: do not subtract them
      again. The last line also gives our size: compare it with the game's (the next named address
      minus this one). Check the frame first: if our `sub esp, N` differs from the game's, size the
      local arrays from the game's slots before anything else (GOTCHAS "Frame size first"; overlay_draw
      62 -> 46).
   d. Change ONE thing in the C, rerun step c, keep the change only if the diff count went down.
      Between two variants with close counts, prefer the one whose size equals the game's (car_laps:
      76 rows at the game's size led to 2, 74 rows at the wrong size led nowhere).
      After every kept change run `.work/venv/bin/python tools/wc/nmrefs.py ww .work/src/WW.OBJ
      NAME=0xADDR`: it must say `same`, or a DIFFER the function's note already explains (inlined
      pal_6bit, a jump table, a folded pointer init). fndiff hides addresses, so a lower count can
      point at the wrong global or call (it caught four behaviour bugs); any other DIFFER is a bug
      to fix, not a gain.
      For many spellings at once use `tools/wc/variants.py` (one compile scores them all; the
      probe file must `#include <string.h>` and the same prototypes as src/ww.c).
   e. When the function MATCHes: run `src/check.sh 2>&1 | grep -c " MATCH"` again. It must be the
      step-b count + 1 or more. If it is lower, your change broke another function (usually by
      -oe inlining): fix that or revert. Raise `src/matched.baseline` to the new count, delete
      the function's `differs:` note, write `MATCH` in its status column in `src/nearmiss.txt`,
      commit.
   f. Budget: about 20 minutes or 20 variants per function, whichever comes first. Then STOP
      probing. KEEP a partial gain: if your best version has fewer rows than the committed one, the
      MATCH count is unchanged and nmrefs says `same`, put that version in src/ww.c and write the
      new count in the first column of src/nearmiss.txt. Otherwise `git checkout -- src/ww.c` (back
      to the committed version). Either way write `SKIP: <cause seen>; tried <what>` in its status
      column, update its `differs:` note (what changed it, what is left, what was tried), commit,
      and take the next open line.
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
   - `short` vs `int` for a local or a loop counter; `char` vs `unsigned char`. A value the game
     computes 16-bit (`mov ax,[g]; dec eax; cwde`) is a `short` local (fn_17928's song index).
   - Two local arrays in each other's slots: a pointer to the one the game has at [esp],
     initialised at entry (`char *nb = numbuf;`, fn_17928 35 -> 9).
   - Operand order of a compare or a product follows the game's load order: `nlaps + 1 > lap`,
     `(lap - 1) * ncells` (car_laps 29 -> 2).
   - An unsigned `cmp eax,1; jbe` + `cmp eax,2; je` chain is a `switch` with `case 0: case 1:`; a
     block the game jumps back into is a duplicated source block the compiler cross-jumped
     (fn_1c0f4 67 -> 14).
   - A global the game keeps in a register across calls: compute in the global itself, drop the
     local copy (minimap, fn_16eac MATCH).
   The full list with addresses is GOTCHAS.md "Levers that produced MATCHes".
   - EAX and EDX roles swapped in an address computation: put the temp into a local that already
     lives in EAX (songs_load: `n = i * 12; f(path, (char *)names + n);` matched).
   - A loop bound written `<= 199` gives `jle`, `< 200` gives `jl`: match the game's compare.
   - Explicit stores in the game's order instead of an initializer; a temp variable
     (`x = cond; if (!x)`) for `setne/movzx` shapes.
   - A byte-loop strcpy from a literal: `strcpy(buf, &"LIT"[0])`; `strcpy(p, "")` is one byte.
   - A function that returns `short` (`test ax, ax` in the caller) needs a `short` prototype,
     also through an alias if the definition returns int.
   Before probing, read GOTCHAS.md "Reading a near-miss diff" (subtract the fake rows) and
   "Dead ends: SKIP at once": a function whose real rows all fall in a dead-end class is SKIPped
   with 0 variants unless you bring an idea its note does not list.

   GOTCHAS.md: only a NEW rule that produced a MATCH, or a new dead-end class, and only after
   grepping the section: extend an existing entry instead of adding one. An entry is the rule plus
   one address, at most 5 lines, in the right subsection (Reading a diff / Probes / Levers / Dead
   ends / General). What you tried for one function goes into its `differs:` note, not GOTCHAS.
   Generic rules also go to ../DREA as in step 3.
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

4. Update `STATUS.md` ONCE, at the end of the iteration, inside your last commit (not a commit of
   its own). Change only the match count line and the `## Next` items (at least 3, small, doable in
   one iteration). Levers, tried spellings and SKIP lists do NOT go into STATUS.md: they live in
   GOTCHAS.md, the `differs:` notes and `src/nearmiss.txt`. Keep it under 60 lines (`wc -l
   STATUS.md`); if it is longer, delete your own additions rather than trimming in extra commits.

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

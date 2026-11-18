# MATCHING: rebuilding WW.EXE's C byte for byte

The pattern is the one fnv_decomp (github.com/momusdigital/fnv_decomp) used and
sam-re's `MATCHING.md` worked through for Turbo Pascal. Here it is adapted to a
1994 Watcom C game in a DOS/4GW LE executable:

1. pin one exact binary;
2. recover the exact toolchain;
3. match the libraries first (they are free matches);
4. keep one list of every function: the plan and the progress meter;
5. one check nobody can argue with: a function is byte-identical or it is not;
6. rewrite functions one at a time in C until each compiles to the same bytes;
7. name things with their evidence, and say what is known about every near miss.

This file is the workflow. The traps and levers it points at live in
`GOTCHAS.md` ("This project's traps"); the numbers live in `src/matched.baseline`,
`src/nearmiss.txt` and `PROGRESS.md`. Do not copy them here.

## 0. The target (CONFIRMED)

`original/WW.EXE` 1.1, checksum in `original/SHA256SUMS`: an MZ stub with
DOS/4GW bound in front of an LE program. Addresses are the LE image's own (code
object at 0x10000), as `tools/le/leparse.py` loads them into `.work/le/ww.*`.
Layout of the code object (`findings/executables.md`, "Code layout"):

| Range | What | Rebuilt from |
|---|---|---|
| 0x10000-0x1025F | `BEGTEXT`, then the initialisers of local arrays | the compiler, from `src/ww.c` |
| 0x10260-0x36A4D | **the game's C: one module**, about 222 functions | `src/ww.c` (this file's subject) |
| 0x36A4E-0x409EE | hand-written assembler, about 40 KB | not started; cannot be matched from C |
| 0x409EF- | libraries: Watcom clib and other modules, in WLINK's order | the libraries themselves |

## 1. The toolchain

- **Compiler: Watcom C/C++32 9.5b** (9.5 plus patches A and B). CONFIRMED that
  it is Watcom C/C++32 (run-time copyright string); 9.5b is STRONGLY SUPPORTED by
  `tools/wc/libmatch.py ww` against CLIB3R.LIB of 9.5 / 9.5a / 9.5b: 72 / 84 / 93
  modules found byte for byte.
- **Options: `-s -3r -oneatx`.** `-s -3r -oailrt` is STRONGLY SUPPORTED (no
  stack check, register calling convention, the optimisations the code shows);
  `-oneatx` and `-oatx` give the same bytes, and `-oneatx` is LIKELY the actual
  command line (`findings/executables.md`, "Options").
- **Install:** `.work/toolchains/inst/WC95B` (also `WC95`, `WC95A` for
  comparisons). Where the floppies and patches come from and how to install
  them under DOSBox: `harness/TRAPS.md`, "Watcom 9.5 install".
- **Driver:** `tools/wc/wcc.sh WCDIR SRCDIR FILE.C [options]` runs WCC386 headless
  under the stock DOSBox (dummy video driver). It writes `FILE.OBJ` and the
  compiler's messages to `WCC.OUT` in SRCDIR, and exits 0 only when the object
  is newer than the start of the run. Every directory can compile in parallel;
  the permuter runs six at a time.
- There is **no per-function optimisation control** in WCC386: every function
  is compiled with the same options (GOTCHAS, dead ends).

## 2. Libraries first

`tools/wc/libmatch.py [-l] ww LIB...` rebuilds each module of an OMF library
with the fixup bytes masked and searches for it in the code object. The clib
modules after 0x409EF match as they are, so they need no source. Which of the
other paragraph-aligned modules after 0x409EF belong to the sound library is
open (`findings/executables.md`, open questions).

## 3. The list of work

- `analysis/ww_names.txt`: every function and global by address,
  `0xADDR name  # LEVEL: why`. `src/check.sh` takes each function's address
  from here by name, so a function without an entry is not compared.
- `src/nearmiss.txt`: every function that compiles but differs, ranked by
  instruction rows (`tools/wc/nearmiss.sh` regenerates the numbers; the status
  column is kept by hand). Status: empty = open, `MATCH`, `SKIP: <causes, what
  was tried>`, `LAYOUT: <why it is not fixable in C>`. The header has the rules.
- `PROGRESS.md` and `docs/WWPROGRESS.HTML`: written by `tools/progress.py` from
  a fresh build, never by hand.

## 4. The check: `src/check.sh`

```sh
src/check.sh            # compile src/ww.c as .work/src/WW.C, compare every function
src/check.sh -v         # also the differing lines
src/check.sh 2>&1 | grep -cE ' MATCH at'    # the number that counts
```

It deletes the old object first (a compile error must not score the previous
one), compiles with the options above and runs `tools/wc/objmatch.py ww`: each
public's bytes, fixup bytes masked, against the game at its address from the
names file. Then it checks that the fixup targets agree with the names
("fixups: N named targets agree").

There is no link yet: `src/ww.c` is compared as one object file, function by
function. A playable build needs the asm module and a WLINK step that
reproduces the library order.

**The rule:** the MATCH count only goes up. `src/matched.baseline` holds it;
raise it in the same commit as the code that adds a MATCH, and only after
`src/check.sh` shows the count one higher. `overnight/check.sh` fails a run
whose count drops below the baseline. Run `tools/wc/nmrefs.py` after every
change (section 6).

## 5. Matching one function

1. **Write the C** in `src/ww.c` at its address order (one file, address
   order, tables first: the module's layout is the compiler's, so the order in
   the file is the order in the binary).
2. **Run `src/check.sh`.** MATCH: raise the baseline, set the `nearmiss.txt`
   status, commit.
3. **Otherwise read the diff before changing anything:**
   `tools/wc/fndiff.py ww .work/src/WW.OBJ NAME 0xADDR`. OURS is the LEFT
   column, the game the RIGHT. The last line is the row count. Subtract the fake
   rows first (masked fixups, stack displacements only): GOTCHAS, "Reading a
   near-miss diff".
   `tools/wc/fncompare.py` answers what fndiff cannot (instruction counts per
   side, same text but different encoding); `tools/wc/objdiff.py` shows the two
   full listings.
4. **Classify the rows** against GOTCHAS: a lever that produced MATCHes before
   ("Levers that produced MATCHes"), or a documented dead end ("Dead ends: SKIP
   at once"). A dead end gets `SKIP:` with the cause, not another evening.
5. **Probe spellings in bulk, decide in place.** `tools/wc/variants.py` scores
   many copies of the function in one compile. Copies appended to a probe file
   can score differently from the same code in `src/ww.c` (shared epilogues,
   different scheduling), so score the unchanged control copy first, and
   compile the winner in place before believing it (GOTCHAS, "Probes").
6. **Write down what you learned.** A near miss keeps a `/* differs: ... */`
   note above it in `src/ww.c` with the causes and the spellings tried; the
   `nearmiss.txt` line gets the count and the status. Commit after each
   function, MATCH or SKIP.

## 6. A near miss is not a proof: `tools/wc/nmrefs.py`

A matching function proves the C means what the game's code does. A near miss
does not: fndiff prints every absolute address as `@`, so a wrong global or a
wrong call can hide in a low row count. `nmrefs.py ww .work/src/WW.OBJ [-v]
[NAME=0xADDR ...]` reduces both sides to the addresses they use (globals,
strings, calls, jump targets), which survive register and slot differences.
With no names it checks every open line of `nearmiss.txt`. It has found four
near misses that behaved differently from the game.

After a change, compare its output with the previous commit's object:
- a new address on one side = a behaviour difference: fix it or do not keep
  the change;
- only counts moving (one more reload of a global, a block laid out
  differently) = the same behaviour, but write it down in `nearmiss.txt`.

## 7. The permuter: `tools/wc/permute.py`

For a near miss whose remaining rows are register or slot choices, a random
search over equivalent spellings can find what hand probes do not
(release_check MATCH, kart_bump 49 -> 36, chat 194 -> 122 in iter 56).

```sh
tools/wc/permute.py [--weighted] [--slack N] [--hill] NAME 0xADDR 0xEND [ROUNDS [COPIES [SEED]]]
tools/wc/permute_all.sh LIST [JOBS [ROUNDS [COPIES]]]   # LIST: "name ourbytes 0xADDR" lines
PERMUTE_FLAGS=--weighted tools/wc/permute_all.sh LIST   # options for every run
grep -H 'final best\|^control' .work/perm_all/*.log
```

Each round compiles `src/ww.c` plus COPIES mutated copies of the function in
`.work/perm/NAME/`. Copies that do not compile are dropped by their `WCC.OUT`
error line, and a source text already scored is not compiled again.

Mutations (borrowed from decomp-permuter's random-mode passes, as regexes
over the source):
- compare operand swap, commuted operands;
- `if (x)` / `while (x)` / `x != 0` / `!x` / `x == 0`;
- `i++` / `++i` / `i += 1` / `i = i + 1`, and `x = x op y` <-> `x op= y`;
- `< N` / `<= N-1`;
- `x = a op b` -> `x = a; x op= b` (scalars only);
- `a = E; b = a;` <-> `b = a = E;`;
- declaration order;
- local types and an `(int)` cast on a compared operand (both `!`);
- an unused pad local;
- a no-op statement (`;`, `if (1) {}`, `do {} while (0);`);
- swapping two independent statements (`!`).

Search: decomp-permuter's random walk rather than pure hill climbing. A sixth
of each round's copies restart from the best; the rest mutate the walk's
current position, which follows the round's winner when it is no worse, and
with probability 0.3 when it is at most `--slack` worse. `--hill` is the old
rule. `--weighted` ranks candidates with decomp-permuter's scorer weights:
5 for a pair with the same mnemonic (register or slot), 60 for a row that only
moved, 100 for an inserted or missing row. The reported score stays in rows,
because rows are what `nearmiss.txt` counts.

A function defined under an alias (showlines is `showlines_def` with
`#pragma aux showlines_def "showlines_"`) is found through that pragma.

**A winner is a candidate, not a result.** Before keeping one:

1. **Put it in place** (replace the function in a copy of `src/ww.c`), compile,
   and compare its fndiff count with the current in-place count. The
   permuter's score is an appended copy's score: ent_sprites_load scored 34 as a
   copy and 75 in place.
2. **Read its source diff, line by line, for behaviour.** A regex mutation can
   change the meaning: until iter 56 the commute and bound mutations broke
   expressions, and the statement swap moved a call before the call it
   depended on and a read before its write (fn_173c4, overlay_draw,
   players_project, netkeys_in, pic_at). The swap now refuses pairs that share
   an identifier or contain a call; still read every swap. A type change
   (`short` <-> `int`) must hold every value the variable takes.
3. **Undo the unsafe mutations and score again;** keep what is left only if it
   still gains.
4. **Apply all the kept winners together, run `src/check.sh` and nmrefs**
   against the previous commit, record each in `nearmiss.txt`
   (`iter N permute.py a -> b: <mutations>`), and commit. A winner with a
   1-row gain that moves nmrefs counts is not worth keeping.

## 8. Names, after the match, with their evidence

Names go in `analysis/ww_names.txt` with an evidence level (CONFIRMED,
STRONGLY SUPPORTED, LIKELY, SPECULATIVE; `METHOD.md` section 2) and the reason.
A function's name in `src/ww.c` is the one in the names file; `fn_XXXXX` and
`vXXXXX` stay until there is evidence for a better one. A match proves the
code, not the name.

## 9. What is left

- The near misses in `src/nearmiss.txt` (`STATUS.md`, "Next").
- The LAYOUT entries in `nearmiss.txt`: code that is instruction-identical but whose cross-region
  jumps or calls differ because the code between is not yet the game's size (fade_out's head at
  0x1bf94 and tail at 0x32cc1). They turn into MATCHes with a link that reproduces the layout.
- The assembler module 0x36A4E-0x409EE: reassemble it (WASM), or port it by
  behaviour; it cannot be matched from C.
- A WLINK link of C, asm and libraries that reproduces `WW.EXE`'s layout: the
  step that turns per-function matches into a shipped file.

# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **193 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  `src/nearmiss.txt` entries (12 MATCH, 30 SKIP, 2 LAYOUT, 56 open); C covers 0x10260-0x36a4d bar
  the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is the hand-written asm.
- Levers that worked (each in GOTCHAS "This project's traps" with its evidence): **a 0/1 assignment
  as a braced `if (cond) { x = 1; } else { x = 0; }` gives the game's `sete al` + `movzx eax,al` in
  x's home, where `x = (cond)` uses a scratch register and adds `mov eax,ebx`** (ctl_label 0x19c68,
  19 -> 0); **split the arithmetic onto a local so the value stays live in its home -
  `m = v88b88 - 1; m *= 0xf3c; seekdat_in(t + m);` wins the `movsx ecx` + `dec ecx` home the
  one-expression form never gives** (track_load 0x27a49, 7 -> 0); **`volatile` on a local for the
  game's `sub esp,4` + `[esp]` spill/reload** (shot_step); **a compare's operand width pins the C
  type, and the type pins the register pairing** (fn_16eac); **a tail-tested loop whose end comes
  from the loop variable** (`end = p + N; do {...} while (p != end);` - track_load's .SIN loop);
  **un-merge a tail-merge by moving one arm's copy of a both-arms statement to that arm's head**
  (race_records_menu); **independent bit tests in the game's load order** (net_input, 15 -> 9).
- Diagnostics that save variants (GOTCHAS iter 16-29): **equal instruction counts + a byte compare
  that starts on a branch displacement = a register home, not a missing instruction** (an absolute
  store from EAX is 5 bytes, `a3`, from any other register 6: shots_move 460 against 462); **line the
  two listings' loads up before probing** - order differs = 9.5b's scheduler (fn_19db8), order equal
  = permuted homes (shots_move, net_recv); **count the rows that are only a stack-slot or fixup
  displacement before spending anything** - same mnemonic both sides = the dead class (showlines
  0x2a268: 10 of 18 rows, 0 variants spent).
- `fndiff.py`'s fake rows: a `[reg +/- @]` pair whose displacement is a fixup is the 0x90 mask filler
  (7 of `self_project`'s 10, 4 of `shots_move`'s 17); tail rows of a function shorter than the game's
  are alignment (`ctl_label`'s 11 nops of 19); a variant can score *fewer* rows by matching less.
- Findings, evidence at the links: serial (findings/serial.md) rings 0x84b00/0x80afa, wrap 0x4000;
  classes (findings/classes.md) `v88bb2` is 1 AMATEUR..5 KID MODE **signed**; car select
  (findings/car_select.md) `ents[8]` 0x73998 is the box; scores (findings/scores.md) `tbl77758`.

## Next

1. `voices_reset` 0x11adc / `kart_move` 0x237e4 (20 rows, the next open near-miss lines): subtract
   the masked-fixup and alignment rows first (iter-29 diagnostic), then decide scheduler vs home;
   `kart_move` shares shot_step's `[esp]` slot - the `volatile int t` lever applies there.
2. `intro` 0x348f4 (21) and `sounds_load` 0x12124 (22): grep the bodies for a 0/1 assignment
   (`= .*== 0`) or a folded multiply (`* 0x`) the iter-29 levers cover, then probe.
3. Try the split-arithmetic lever on the functions whose note says a load home never flips
   (`pcx_load`, `shots_move`, `net_recv`, `netkeys_*`): `x = <expr>; x *= <factor>;` as separate
   statements has never been tried there (GOTCHAS iter 29).
4. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer.

## BLOCKED (dead-end classes, do not re-probe)

- Stack-slot permutation of locals (`release_check`, `wtext`, `enter_name`, `scores_screen`,
  `fn_1c0f4`, `showlines`): Watcom's scalar slot order is neither declaration nor use order; in
  `showlines` 10 of 18 rows are only `[esp+..]` displacements, so its other rows cannot buy a MATCH.
- Two locals' register pairing, a tie-break and not a translation error (`pcx_load` ESI/EDI,
  `com_wait_byte`, `com_expect`, `self_project`, `shots_clear`, `fade_in`, `drawsprite_clip`,
  `kart_draw`, `shots_move`'s bounce vector): 10-25 spellings each move nothing. Check the C types
  first (iter 27's width rule) and the split-arithmetic form (iter 29) before logging one.
- One local's home and constant hoisting at one variable (`netkeys_out`/`netkeys_pack`/`netkeys_in`
  c in EDX vs EAX; `fn_1be3c` 0 in a pushed EDI; `race_records_menu` the pooled "" byte in EDX vs AL;
  `fn_16eac` 0x12 in SI vs folded; `fn_19db8` EAX/EDX, where both sides home the FIRST load in EDX
  and only the emission order differs): 9.5b has **no explicit register variables in any scope**
  (E1009 in a function, E1059 at file scope) and a `volatile` alias does not stop a fold. Short
  encodings explain size gaps: AL owns `a0`/`a2`, EAX owns `a1`/`a3` (netkeys_pack, shots_move).
- Instruction scheduling in one straight-line head (`in_sector`): 9.5b interleaves two independent
  index/load chains where the game keeps each pair adjacent, and a statement written **below** a
  branch sinks past the whole branch - never between its `test` and its `jcc`. Watcom does not CSE a
  global array read across separate conditions. Same class: a prologue load's place among the loop
  inits (`fn_16eac`), and an extra local bought to move one arithmetic step into another register,
  which costs frame and spills (`kart_draw`, 17 -> 28); adding a local also re-colours unrelated
  temps far away (`track_load`'s v88b88 load), so score the whole function.
- Address constants 9.5b folds that the game materialises: `&netstats[1]` as one relocated immediate
  vs the game's `mov esi,0x3c` + `add esi,0x7c818` (showlines 0x2a359), and the pooled "" byte
  (race_records_menu). No C spelling found that forces the two-instruction form.
- SKIP at once (0 variants): the classes above and a global the game keeps in a register across a
  loop (QUESTIONS 2). SKIPped (row counts in `src/nearmiss.txt`): music_jingle, track_setup,
  classes_menu, fn_182d0, line_draw, net_recv, pcx_load, demo_play, view_at_curpos, wtext,
  release_check, com_wait_byte, com_expect, self_project, netkeys_out, netkeys_pack, fn_1be3c,
  shot_step, shots_clear, fade_in, in_sector, net_input, drawsprite_clip, fn_16eac, demo_mode,
  kart_draw, shots_move, fn_19db8, showlines.

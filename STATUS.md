# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **191 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  `src/nearmiss.txt` entries (10 MATCH, 27 SKIP, 2 LAYOUT, 61 open); C covers 0x10260-0x36a4d bar
  the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is the hand-written asm.
- Levers that worked (GOTCHAS "This project's traps" has each with its evidence): **`volatile` on a
  local to force the game's `sub esp,4` + `[esp]` spill/reload of a temp** (shot_step); **a compare's
  operand width pins the C type, and the type pins the register pairing** (fn_16eac: the game's
  `cmp ebx,ecx` 0x16eec says its counter is `int`, and `int cnt` moved it to EBX with the `movsx`'d
  bound in ECX, 17 -> 11, iter 27); **moving one arm's copy of a statement that ends both arms to the
  head of that arm un-merges the tail-merge** (race_records_menu); **independent bit tests in the
  game's load order** (net_input, 15 -> 9); a statement the call does not read goes **below** it
  (net_key), and the load of the next statement **above** the assignment it splits (fn_16ca0).
- Not reachable so far (a `differs:` note plus a SKIP line list the spellings spent): one or two
  locals' home, a dead incoming-register copy (fn_182d0), one moved load (track_setup, music_jingle,
  demo_play), the v88bb2/v7edb0 pair (classes_menu), the int shift into `err` (line_draw), two load
  orders (shot_step), a straight-line head's schedule (in_sector, net_input), one slot reload's
  register pairing (drawsprite_clip), 9.5b folding a constant into a store the game does from a
  register and a dead tail load's register (fn_16eac), and the 16-bit word load the game puts in
  f375cd's EDX argument - every C spelling of that argument extends it, and making the aux parameter
  16-bit pushes it instead (demo_mode 0x1e472, .work/probe_f375).
- `fndiff.py`'s fake rows (GOTCHAS iter 19-26): a `[reg +/- @]` pair whose displacement is a fixup is
  the 0x90 mask filler (7 of `self_project`'s 10, 4 of `kart_draw`'s 17); tail rows of a function
  shorter than the game's are alignment (4 of `fn_16eac`'s 11); a variant can score *fewer* rows by
  matching less (in_sector's `is_idxloc`). `check.sh` is the verdict.
- Findings, evidence at the links: serial (findings/serial.md) rings 0x84b00/0x80afa, wrap 0x4000,
  `com_isr`/`com_isr_fifo` identical; classes (findings/classes.md) `v88bb2` is 1 AMATEUR..5 KID MODE
  **signed**; car select (findings/car_select.md) `ents[8]` 0x73998 is the selection box; scores
  (findings/scores.md) `tbl77758` rows of 0x450 = 24 records of 0x2e; menus (findings/menus.md)
  `fn_182d0`'s 4th argument is a **word** (0x182e9); `pal_shift` has no caller (QUESTIONS 15);
  findings/sound.md missing.

## Next

1. `shots_move` 0x2602c (17 rows, first open line): read the rows, and compare every compare's
   operand width with the C types before calling a register home the tie-break class (iter 27).
2. `fn_19db8` 0x19db8 and `track_load` 0x279e8 (18 rows each, next open lines): same first pass;
   fn_19db8's note already blames the EAX coin flip, so look there for a width mismatch too.
3. `kart_move` 0x237e4 (20 rows): the same `[esp]` slot as shot_step - apply the `volatile int t`
   lever there (shot_step stopped at 2 rows, see BLOCKED).
4. `race_records_menu` 0x1d13c (12 rows): 5 are the BLOCKED EDX-vs-AL home of the pooled "" byte, 4
   the un-merge spelling now in `src/ww.c` - decide keep or revert before more variants.
5. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer; the original under the harness (LE deltas, save state).

## BLOCKED (dead-end classes, do not re-probe)

- Stack-slot permutation of locals (`release_check` 0x34458, `wtext` 0x146d4, `enter_name`,
  `scores_screen`, `fn_1c0f4`): Watcom's scalar slot order is neither declaration nor use order.
- Two locals' register pairing, a tie-break and not a translation error (`pcx_load` ESI/EDI,
  `com_wait_byte`, `com_expect`, `self_project`, `shots_clear`, `fade_in`, `drawsprite_clip`,
  `kart_draw` EBX/ECX for f7a and the stride): 10-25 spellings each move nothing. Check the C types
  first (iter 27's width rule); otherwise two pushed-register homes are as unpinnable as the EAX
  choice, and a comma `for`'s initializer order only swaps the two zeroing `xor`s.
- One local's home and constant hoisting, the same tie-break at one variable (`netkeys_out`/
  `netkeys_pack`/`netkeys_in` c in a saved EDX vs EAX; `fn_1be3c` the constant 0 in a pushed EDI;
  `race_records_menu` the pooled "" byte in EDX vs AL; `fn_16eac` 0x12 in SI vs folded into
  `mov word [nplayers],0x12`): 9.5b has **no explicit register variables in any scope** (E1009 in a
  function, E1059 at file scope) and a `volatile` alias does not stop a fold. A DL home also
  explains a size gap - AL owns the short forms (netkeys_pack 0x2da45-0x2da85).
- Instruction scheduling in one straight-line head (`in_sector` 0x24a08): 9.5b interleaves two
  independent index/load chains where the game keeps each pair adjacent, and a statement written
  **below** a branch sinks past the whole branch - never between its `test` and its `jcc`, so a load
  in a game branch shadow is not a source position. Watcom does not CSE a global array read across
  separate conditions, and with volatile reads pairs each load with its mask where the game batches
  them. Same class: a prologue load's place among the loop inits (`fn_16eac`), a dead tail load's
  register (AX vs the game's SI), and an extra local bought to move one arithmetic step into another
  register, which costs frame and spills (`kart_draw`, 17 rows -> 28).
- SKIP at once (0 variants): the classes above and a global the game keeps in a register across a
  loop (QUESTIONS 2). SKIPped (row counts in `src/nearmiss.txt`): music_jingle, track_setup,
  classes_menu, fn_182d0, line_draw, net_recv, pcx_load, demo_play, view_at_curpos, wtext,
  release_check, com_wait_byte, com_expect, self_project, netkeys_out, netkeys_pack, fn_1be3c,
  shot_step, shots_clear, fade_in, in_sector, net_input, drawsprite_clip, fn_16eac, demo_mode, kart_draw.

# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **191 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`); 100
  `src/nearmiss.txt` entries (10 MATCH, 29 SKIP, 2 LAYOUT, 59 open); C covers 0x10260-0x36a4d bar
  the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is the hand-written asm.
- Levers that worked (GOTCHAS "This project's traps" has each with its evidence): **`volatile` on a
  local to force the game's `sub esp,4` + `[esp]` spill/reload of a temp** (shot_step); **a compare's
  operand width pins the C type, and the type pins the register pairing** (fn_16eac, 17 -> 11); **a
  tail-tested loop whose end is computed from the loop variable** (`p = arr; end = p + N; do {...}
  while (p != end);` - track_load's .SIN loop, 18 -> 7 rows, iter 28); **moving one arm's copy of a
  statement that ends both arms to that arm's head un-merges the tail-merge** (race_records_menu);
  **independent bit tests in the game's load order** (net_input, 15 -> 9); a statement the call does
  not read goes **below** it (net_key).
- Diagnostics that save variants (GOTCHAS iter 28): **equal instruction counts + a byte compare that
  starts on a branch displacement = a register home, not a missing instruction** (an absolute store
  from EAX is 5 bytes, `a3`, from any other register 6: shots_move 460 against 462); **line the two
  listings' loads up before probing** - order differs = 9.5b's scheduler (fn_19db8), order equal =
  permuted homes (shots_move, net_recv).
- Not reachable so far (each has a `differs:` note and a SKIP line listing the spellings spent): a
  local's home or a dead incoming-register copy (fn_182d0), one moved load (track_setup,
  music_jingle, demo_play), the v88bb2/v7edb0 pair (classes_menu), the int shift into `err`
  (line_draw), two load orders (shot_step), a straight-line head's schedule (in_sector, net_input),
  one slot reload's pairing (drawsprite_clip), a constant folded into a store the game does from a
  register (fn_16eac), the 16-bit word load in f375cd's EDX argument (demo_mode), and homes no
  spelling moves (shots_move's EBX/EDX/EAX triple, fn_19db8's EAX/EDX pair).
- `fndiff.py`'s fake rows (GOTCHAS iter 16-28): a `[reg +/- @]` pair whose displacement is a fixup is
  the 0x90 mask filler (7 of `self_project`'s 10, 4 of `kart_draw`'s 17, 4 of `shots_move`'s 17, 3 of
  `track_load`'s 7); tail rows of a function shorter than the game's are alignment (4 of
  `fn_16eac`'s 11); the game's `mov eax,eax` filler after the last matched byte costs a row; a
  variant can score *fewer* rows by matching less. `check.sh` is the verdict.
- Findings, evidence at the links: serial (findings/serial.md) rings 0x84b00/0x80afa, wrap 0x4000;
  classes (findings/classes.md) `v88bb2` is 1 AMATEUR..5 KID MODE **signed**; car select
  (findings/car_select.md) `ents[8]` 0x73998 is the selection box; scores (findings/scores.md)
  `tbl77758` rows of 0x450 = 24 records of 0x2e; `pal_shift` has no caller (QUESTIONS 15);
  findings/sound.md missing.

## Next

1. `track_load` 0x279e8 (7 rows, first open line): one pair is real - the v88b88 load at 0x27a49
   wants `movsx ecx` + `dec ecx` and takes EAX + `lea ecx,[eax-1]` since the .SIN loop gained an end
   pointer. Try dropping the `pal` local (it only caches `p7d718`), or repair the multiply chain of
   `t + v88b88 * 0xf3c - 0xf3c` (that spelling gets ECX back, 30 rows as-is).
2. `showlines` 0x02a268 / `ctl_label` 0x019c68 / `voices_reset` 0x011adc (18-20 rows): first pass
   with the iter-28 diagnostic - subtract the masked-fixup and filler rows, then decide scheduler vs
   home before spending variants.
3. `kart_move` 0x237e4 (20 rows): the same `[esp]` slot as shot_step - apply the `volatile int t`
   lever there (shot_step stopped at 2 rows, see BLOCKED).
4. Re-write loops that compare against `array + N` in the track_load shape (`do/while` +
   `end = p + N`): grep the near-miss bodies for `!= x + 0x` in `src/ww.c` first.
5. Inventory: WACKY.DAT archives and formats, the `42.GAM`/`20.GAM` records (`v88bd6` = `datfind`'s
   index), "BSBCS.PCX", `tbl77758`'s writer.

## BLOCKED (dead-end classes, do not re-probe)

- Stack-slot permutation of locals (`release_check` 0x34458, `wtext` 0x146d4, `enter_name`,
  `scores_screen`, `fn_1c0f4`): Watcom's scalar slot order is neither declaration nor use order.
- Two locals' register pairing, a tie-break and not a translation error (`pcx_load` ESI/EDI,
  `com_wait_byte`, `com_expect`, `self_project`, `shots_clear`, `fade_in`, `drawsprite_clip`,
  `kart_draw` EBX/ECX for f7a and the stride, `shots_move` the bounce vector's EBX/EDX/EAX): 10-25
  spellings each move nothing. Check the C types first (iter 27's width rule); otherwise two
  pushed-register homes are as unpinnable as the EAX choice.
- One local's home and constant hoisting, the same tie-break at one variable (`netkeys_out`/
  `netkeys_pack`/`netkeys_in` c in a saved EDX vs EAX; `fn_1be3c` the constant 0 in a pushed EDI;
  `race_records_menu` the pooled "" byte in EDX vs AL; `fn_16eac` 0x12 in SI vs folded into
  `mov word [nplayers],0x12`; `fn_19db8` EAX/EDX, where both sides home the FIRST load in EDX and
  only the emission order differs): 9.5b has **no explicit register variables in any scope** (E1009
  in a function, E1059 at file scope) and a `volatile` alias does not stop a fold. Short encodings
  explain size gaps: AL owns `a0`/`a2`, EAX owns `a1`/`a3` (netkeys_pack, shots_move).
- Instruction scheduling in one straight-line head (`in_sector` 0x24a08): 9.5b interleaves two
  independent index/load chains where the game keeps each pair adjacent, and a statement written
  **below** a branch sinks past the whole branch - never between its `test` and its `jcc`. Watcom
  does not CSE a global array read across separate conditions. Same class: a prologue load's place
  among the loop inits (`fn_16eac`), a dead tail load's register, and an extra local bought to move
  one arithmetic step into another register, which costs frame and spills (`kart_draw`, 17 -> 28).
  Adding a local also re-colours unrelated temps far away (`track_load`'s v88b88 load), so score the
  whole function.
- SKIP at once (0 variants): the classes above and a global the game keeps in a register across a
  loop (QUESTIONS 2). SKIPped (row counts in `src/nearmiss.txt`): music_jingle, track_setup,
  classes_menu, fn_182d0, line_draw, net_recv, pcx_load, demo_play, view_at_curpos, wtext,
  release_check, com_wait_byte, com_expect, self_project, netkeys_out, netkeys_pack, fn_1be3c,
  shot_step, shots_clear, fade_in, in_sector, net_input, drawsprite_clip, fn_16eac, demo_mode,
  kart_draw, shots_move, fn_19db8.

# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries incl. the Apogee Sound System (findings/executables.md).
- Classes/modes (findings/classes.md): `v88bb2` = 1 AMATEUR ... 5 KID MODE (a **signed** short),
  `classes_menu` 0x19eac is that screen; `laps_menu` 0x1a148 (identical) sets `nlaps` 6/8/10.
- Car select (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b, one 6861-byte C function, `ents[0..7]`
  car icons plus **`ents[8]` as the selection box** (0x73998); `v7d338[car]` = per-car lock (0x34463).
- Race setup (findings/game-loop.md, both near misses): `fn_1be3c` builds the start grid `v70720[0..7]`
  (permutation of 0..7, `myplayer` first, `v88c6a` second in 2p, rest `rand()`-drawn); `fn_1bf00` sets
  `players[i].f64`/`.face`.
- Scores (findings/scores.md): `tbl77758` rows of 0x450 = 24 records of 0x2e (stamp +6 from `fn_1c994`,
  name +0x1a), row `v88bb6 + v7ecb4`; `fn_1cad0` picks one, `enter_name` 0x1cb44 (21 slot diffs) and
  `scores_screen` 0x1ce14 (97 diffs) insert `v7ed5c` among the group's first four - beating none
  still gets the last slot.
- Record books (findings/scores.md): the drivers-by-class menu in `fn_1f858` (items 0x61410-0x61460)
  puts its choice in `v88c80`; 1-3 call `race_records_menu` 0x1d13c (`fn_1d704(class, v7ecb4 +
  choice)`, near miss), 4 calls `trial_records_menu` 0x1d340 (byte-identical: metal, then five
  "TRACK n" rows, then `f2e4b8(metal + v7ecb4, track, 0)`). Menu rows are `mrows[][0x3c]` at
  0x7bc00 indexed from 1 (`mnames` = `mrows[1]`); names are `strcpy(prefix)` + `strcat` + padding.
- Menus/help (findings/menus.md): `fn_1c0f4` 0x1c0f4-0x1c88b = the F1 controls screen; control names
  in 84-byte records at 0x77432.
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of 0x3e80
  bytes A0000+`pal_b` to A0000+`pal_c`; its flippers stay `extern` (-oe loses `net_end`); page
  flips are `if`/`else` pairs of stores, never ternaries.

## Next

Take the first open item when QUEUE.md has none open; mark items CLOSED or BLOCKED.
1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1bf7d and 0x1c0f4-0x1f823 plus 0x1f858-0x2130c and 0x21310-0x2847f, 135
   functions identical and 67 near misses (`src/check.sh`; the register/scheduling ones are
   QUESTIONS.md 2, 3, 9, 10). arg1=EAX arg2=EDX arg3=EBX arg4=ECX; stack args `ret 4`; player 0x68.
2. Next in address order: 0x28480 (`track_around` 0x283a4 index registers; `kartsets_load` 0x28038 outer-loop strength reduction; `track_load` 0x279e8 two register causes; `track_pics_load` 0x277b8 2 blocks; `pcx_load` 0x27060 one register pair; `self_project` 0x26a3c one dec register; `lights_project` 0x266a8 18 blocks; `shots_project` 0x2632c like players_project; `anim_tick` 0x261fc one swapped pair; `shots_move` 0x2602c 7 blocks; `shot_step` 0x25e6c a near miss by one stack temp; `players_project` 0x255d4 and `ents_project` 0x25a78 near misses: v7d800 not promoted, QUESTIONS 2; `kart_draw` 0x24bcc 8 blocks)
   `credits_screen` 0x1efb0 is a near miss, slots; `volume_menu` 0x1e760 is
   a near miss by one frame-wait load; `demo_mode` 0x1e2e4 is a
   near miss, 3 causes in its note; `dial`
   0x1deb0 and `link_open` 0x1dae8 now match through the com_getc alias), then
   `f2e4b8` (LIKELY the per-track trial table; callers 0x1911f, 0x1d6ca, 0x31e98). `fn_1d704`
   0x1d704 is a near miss (the `which` spill, QUESTIONS 12; its prototype stays unprototyped).
3. `race_records_menu` 0x1d13c, one block left (0x1d178-0x1d196): the game loads the pooled "" byte
   0x600ac into EDX, ours into EAX, which lets -oe tail-merge a store (GOTCHAS iter 13 - and the six
   probes in `.work/rrmv.c` were useless, that file has no `#include <string.h>`). Diff with
   `tools/wc/fncompare.py` (instruction counts per side, unlike fndiff).
4. Local-slot probes (QUESTIONS 9, 10): `enter_name` 21 diffs, all displacements; `scores_screen` 97
   diffs (five causes in its `differs:` note). Vary one lever per try (`short` vs `int`, etc.).
5. `fn_1c0f4` (the help screen, near miss of exactly the game's size): 67 aligned diffs, five
   block-shape causes in its `differs:` note.
6. QUESTIONS 6 has a workaround: call through a `#pragma aux x "name_"` alias where the game keeps a
   call (com_getc, kart_anim2, readdat/seekdat/telldat/datsize are done). Not yet tried: the
   other near misses whose notes blame -oe inlining (grep `inlin` in src/ww.c).
7. One call-shape trap left (`v88bb2` is now `short`, which fixed 0x1d120): `f375cd`'s aux
   `parm [edx]` forces mov+movzwl over a 16-bit load (0x19506, 0x1ce57, 0x1d73e, 0x1e472, 0x1e816;
   `pal_a short` costs a match, `parm [eax] [dx]` adds instructions in all four near misses);
   the `telldat() + datsize()` one at 0x194c8 is gone with the aliases.
8. Inventory: archives and formats (WACKY.DAT), strings - the `42.GAM`/`20.GAM` dat records
   (`v7eca0` 1/2/3, 0x34463-0x344d4, `v88bd6` = `datfind`'s index), "BSBCS.PCX", `tbl77758`'s writer.
9. Get the original running under the harness; record the boot's segments or LE deltas and a first
   save state in `captures/`. Cheap checks: eight cars selectable off-net when registered
   (`v7e574 == 0`), and a `tbl77758` record read (what the +6 stamp holds, LIKELY).

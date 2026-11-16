# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries incl. the Apogee Sound System (findings/executables.md).
  Compiling the whole file with `-3` instead of `-3r` gives a byte-identical object (182 MATCH), so
  the CPU-reorder letter is not what moves instructions - do not re-test it.
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
- Music (findings/sound.md still unwritten; the compiler fact is in GOTCHAS): `music_start` 0x1180c and `music_play` 0x11834 are byte-identical; the
  game inlines `music_start` (scan the code for `66 c7 05 cc8b08 0000`, the `mov word [0x88bcc],0`
  of its body: 20 hits, about half decode as real inline copies), and at the four inlined
  `music_start(song, 0)` copies it keeps
  `xor edx,edx` **before** the two stores (0x11885, 0x2b6a3, 0x2cad6, 0x2e729) while 9.5b sinks it -
  one instruction, in `music_jingle`, `duck_screen`, `race_results`, `trial_book` (GOTCHAS).
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of 0x3e80
  bytes A0000+`pal_b` to A0000+`pal_c`; its flippers stay `extern` (-oe loses `net_end`); page
  flips are `if`/`else` pairs of stores, never ternaries.

## Next

Take the first open item when QUEUE.md has none open; mark items CLOSED or BLOCKED.
1. Near misses: take the first line with an empty status in `src/nearmiss.txt` (ranked by
   `tools/wc/nearmiss.sh`, smallest diff first; procedure in overnight/prompt.md step 2). The C
   rebuild covers 0x10260-0x36a4d except the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857:
   182 functions identical, 94 open near misses (3 SKIP, 2 LAYOUT, 1 MATCH marked). Next open:
   `anim_tick` 0x261fc (3), `fn_182d0` 0x182d0 / `line_draw` 0x21f04 / `msgbox` 0x2a414 /
   `net_recv` 0x2db00 (4 each). 0x36a4e on is the hand-written assembly module.
2. BLOCKED, do not re-probe (each has a `differs:` note in `src/ww.c` and a GOTCHAS entry):
   `music_jingle` 0x11884 (2), `track_setup` 0x124e8 (2), `classes_menu` 0x19eac (2) - all three are
   one instruction that 9.5b schedules at a different point: a zero inline-argument constant
   (`-oe` sinks it, a non-zero one stays), the switch subject load, and the second of two adjacent
   stores around an inline strcpy. `demo_rec`, `pic_show` are LAYOUT (address of a neighbouring
   function only).
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

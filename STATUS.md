# STATUS

Short: where the project stands, and what is next. History is in git; do not
keep a session diary here.

## State

- Executables identified: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, options
  `-s -3r -oailrt` (`-oneatx` used; findings/executables.md). Layout: one C module, an asm block,
  libraries incl. the Apogee Sound System; method: ../DREA METHOD.md 9 then 10.
- Classes/modes (findings/classes.md): `v88bb2` = 1 AMATEUR ... 5 KID MODE, `classes_menu` 0x19eac is
  that screen (`fn_19db8` sets `cls_name`, `v7ece0`, `tbl7e730` index `v88b84`); `laps_menu` 0x1a148
  (byte-identical) sets `nlaps` to 6/8/10 - 3 in KID MODE, 6 for TIME TRIAL.
- Car select (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b, one 6861-byte C function, uses
  `ents[0..7]` as car icons plus **`ents[8]` as the selection box** (0x73998), then the other menus.
  `v7d338[car]` is the per-car lock (shareware 0,1,5,6; `v7e574` CONFIRMED, 0x34463-0x344d4).
- Race setup (findings/game-loop.md, both near misses): `fn_1be3c` 0x1be3c builds the start grid
  `v70720[0..7]` (permutation of 0..7, `myplayer` first, `v88c6a` second in 2p, the rest `rand()`-drawn
  or counting up under splitscreen); `fn_1bf00` sets `players[i].f64` and `.face`.
- Scores (findings/scores.md): `tbl77758` rows of 0x450 = 24 records of 0x2e (words +0/+2/+4, strings
  +6, +0x1a), row index `v88bb6 + v7ecb4`; `fn_1cad0` (identical) picks one, `fn_1c994` the stamp,
  `enter_name` 0x1cb44 (rebuilt, 21 slot diffs) types the name, DOS 2509h at 0x1cc2f restores INT 9.
- Menus/help (findings/menus.md): `fn_1c0f4` 0x1c0f4-0x1c88b is the F1 controls screen (twelve
  80-byte frame slots + five dwords); its control names are six 14-byte strings in 84-byte records at
  0x77432; `0x1c88c`/`0x1c968` are two more functions nothing references (identical; GOTCHAS).
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of
  0x3e80 bytes from A0000+`pal_b` to A0000+`pal_c` (`pal_a`-`pal_d` are page offsets). Its flippers
  `fn_1c068`/`fn_1c0ac` stay `extern`: defining them lets `-oe` lose `net_end` (GOTCHAS). The menu
  page flip is an `if`/`else` pair of stores, never a ternary (18 sites; 0 `mov eax,0x3e80`).

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1bf7d and 0x1c0f4-0x1ce11, 87
   functions byte-identical and 38 near misses (`src/check.sh`; `differs:` marks; the
   register/scheduling ones are QUESTIONS.md 2, 3). Convention arg1=EAX arg2=EDX arg3=EBX
   arg4=ECX; stack args pushed by the caller, popped by the callee (`ret 4`); struct player
   pack(1) 0x68, face 0x98.
2. Next in address order: 0x1ce14, 807 bytes (`ret` at 0x1d13a; GOTCHAS iter 10 - `starts.py` also
   lists an inner 0x1cebc): it calls `enter_name` at 0x1ce78 and folds the three groups of
   `tbl77758` at 0x1ce9a-0x1cf2d. Write it, then `tools/wc/fndiff.py ww .work/src/WW.OBJ <name>_`.
3. `enter_name`'s local slot permutation (QUESTIONS 9): 21 diffs left, all displacements; vary one
   lever at a time (`short` vs `int`, a local only used as an argument), probes via the GUIDE recipe.
4. `fn_1c0f4` (the help screen, near miss of exactly the game's size): 67 aligned diffs, five
   block-shape causes in its `differs:` note.
5. The two-module experiment for QUESTIONS 6: the palette pair (and ideally `readdat`) from a
   second OMF module so the calls survive `-oe`; that makes `fn_1c068`/`fn_1c0ac` matches and takes
   11 bytes off each of the 24 `readdat` sites in fn_17928/fn_1832c.
6. Fix the two call-shape traps before the menus are retried: the `f375cd` aux `parm [edx]` forces
   mov+movzwl over a 16-bit partial load (0x19506), `telldat() + datsize() - 0x300` accumulates in
   the wrong register (0x194c8); `cars_menu` is now 678 diffs (was 749, GOTCHAS flip form).
7. Inventory: archives and file formats (WACKY.DAT), strings. What are the `42.GAM`/`20.GAM` dat
   records (`v7eca0` 1/2/3, 0x34463-0x344d4, `v88bd6` = `datfind`'s index)? The "BSBCS.PCX" record?
8. Get the original running under the harness; record the boot's segments or LE deltas and a first
   save state in `captures/`. Cheap check: registered release should have all eight cars selectable
   off-net (`v7e574 == 0`) and "PLEASE READ ORDERING INFO" unreachable off-net.
9. Copy this session's generic traps (GOTCHAS: the ternary-vs-`if`/`else` store, the local slot map)
   into `../DREA/reference/DOS.md` - BLOCKED: the overnight policy blocks writes outside this repo.

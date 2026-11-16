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
  +6, +0x1a), row index `v88bb6 + v7ecb4`; `fn_1cad0` (identical) picks one, `fn_1c994` the stamp.
- Menus/help (findings/menus.md): `fn_1c0f4` 0x1c0f4-0x1c88b is the F1 controls screen (twelve
  80-byte frame slots + five dwords); its control names are six 14-byte strings in 84-byte records at
  0x77432; `0x1c88c`/`0x1c968` are two more functions nothing references (identical; GOTCHAS).
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of
  0x3e80 bytes from A0000+`pal_b` to A0000+`pal_c` (`pal_a`-`pal_d` are page offsets). Its flippers
  `fn_1c068`/`fn_1c0ac` stay `extern`: defining them lets `-oe` lose `net_end` (GOTCHAS).

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1bf7d plus 0x1c0f4-0x1cb42 and
   0x1c88c-0x1c993, 87 functions byte-identical and 37 near misses (`src/check.sh`; `differs:`
   marks; the register/scheduling ones are QUESTIONS.md 2, 3). Convention arg1=EAX arg2=EDX arg3=EBX
   arg4=ECX; stack args pushed by the caller, popped by the callee (`ret 4`); struct player pack(1)
   0x68, face 0x98.
2. `fn_1c0f4` (the help screen) is a near miss of exactly the game's size: 67 aligned instruction
   diffs, five block-shape causes in its `differs:` note; iterate with
   `tools/wc/fndiff.py ww .work/src/WW.OBJ fn_1c0f4_`. Then, in address order, 0x1cb44 (696 B,
   "PLEASE ENTER NAME.." 0x60d8c) and 0x1ce14 (808 B, score-table inserter); 0x77432's writer is
   QUESTIONS.md 8.
3. The two-module experiment for QUESTIONS 6: the palette pair (and ideally `readdat`) from a second
   OMF module, so the calls survive `-oe`; that makes `fn_1c068`/`fn_1c0ac` matches and takes 11 bytes
   off each of the 24 `readdat` sites in fn_17928/fn_1832c.
4. Fix the two known call-shape traps before the menus are retried: the `f375cd` aux `parm [edx]`
   forces mov+movzwl where the game does a 16-bit partial load (0x19506), and `telldat() + datsize()
   - 0x300` accumulates in the wrong register (0x194c8).
5. The store/register near misses: classes_menu's `v7edb0` schedule (QUESTIONS 3), fn_19db8's
   register home (2), fn_1c994's `mov ebp,edi` placement, `cars_menu`'s 27-slot permutation (5).
   Batch-probe with `tools/wc/variants.py WW.OBJ ww 0x19eac 0x1a147 name...` (~30 shapes, 4 s).
6. Inventory: archives and file formats (WACKY.DAT), strings. What are the `42.GAM`/`20.GAM` dat
   records (`v7eca0` 1/2/3, 0x34463-0x344d4, `v88bd6` = `datfind`'s index)? The "BSBCS.PCX" record?
7. Get the original running under the harness; record the boot's segments or LE deltas and a first
   save state in `captures/`. Cheap check: registered release should have all eight cars selectable
   off-net (`v7e574 == 0`) and "PLEASE READ ORDERING INFO" unreachable off-net.
8. Copy this session's generic traps (GOTCHAS: `-oe` inlines anything you define; DOSBox 8.3 file
   names) plus the earlier twelve into `../DREA/reference/DOS.md` - BLOCKED: the overnight policy
   blocks writes outside this repo.

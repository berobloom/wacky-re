# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables identified: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, options
  `-s -3r -oailrt` (`-oneatx` used; findings/executables.md). Layout: one C module, an asm block,
  libraries incl. the Apogee Sound System; method: ../DREA METHOD.md 9 then 10.
- Classes/modes (findings/classes.md): `v88bb2` = 1 AMATEUR ... 5 KID MODE (a **signed** short) and
  `classes_menu` 0x19eac is that screen; `laps_menu` 0x1a148 (identical) sets `nlaps` 6/8/10.
- Car select (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b, one 6861-byte C function, uses
  `ents[0..7]` as car icons plus **`ents[8]` as the selection box** (0x73998); `v7d338[car]` is the
  per-car lock (shareware 0,1,5,6; `v7e574` CONFIRMED, 0x34463-0x344d4).
- Race setup (findings/game-loop.md, both near misses): `fn_1be3c` builds the start grid
  `v70720[0..7]` (permutation of 0..7, `myplayer` first, `v88c6a` second in 2p, the rest `rand()`-drawn
  or counting up under splitscreen); `fn_1bf00` sets `players[i].f64` and `.face`.
- Scores (findings/scores.md): `tbl77758` rows of 0x450 = 24 records of 0x2e (words +0/+2/+4, the
  `fn_1c994` stamp +6, the `enter_name` name +0x1a), row `v88bb6 + v7ecb4`; `fn_1cad0` picks one,
  `enter_name` 0x1cb44 (21 slot diffs) types the name and `scores_screen` 0x1ce14 (97 diffs) inserts
  `v7ed5c` among the group's first four - higher better; beating none still gets the last slot.
- Menus/help (findings/menus.md): `fn_1c0f4` 0x1c0f4-0x1c88b is the F1 controls screen (twelve
  80-byte slots + five dwords), its control names six 14-byte strings in 84-byte records at 0x77432.
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of
  0x3e80 bytes A0000+`pal_b` to A0000+`pal_c` (`pal_a`-`pal_d` are page offsets); its flippers
  `fn_1c068`/`fn_1c0ac` stay `extern` (-oe loses `net_end`, GOTCHAS); page flips are `if`/`else`
  pairs of stores, never ternaries (18 sites, 0 `mov eax,0x3e80`).

## Next

Take the first open item when QUEUE.md has none open; mark items CLOSED or BLOCKED.

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1bf7d and 0x1c0f4-0x1d13a, 87
   functions identical and 39 near misses (`src/check.sh`; the register/scheduling ones are
   QUESTIONS.md 2, 3, 9, 10). arg1=EAX arg2=EDX arg3=EBX arg4=ECX; stack args pushed by the caller
   and popped by the callee (`ret 4`); struct player pack(1) 0x68.
2. Next in address order: 0x1d13c (0x204 bytes to 0x1d340), 0x1d340, then `fn_1d704` - the score
   table screen (LEAD.PCX 0x1d71f, `add v7d8a8, 0x2e` 0x1da3c); `starts.py`'s `after-ret` entries
   0x1d6d0/0x1d6f0 need the prologue/`ret`/padding rule (GOTCHAS iter 10). Diff with `fndiff.py`.
3. Local-slot probes (QUESTIONS 9 and 10): `enter_name` 21 diffs, all displacements; `scores_screen`
   97 diffs, its two scalar slots swapped and the EAX/EDX roles of the shift/insert blocks reversed
   (five causes in its `differs:` note; `do`/`while` and `for (;;)` + break both score worse). Vary
   one lever at a time (`short` vs `int`, a local only used as an argument).
4. `fn_1c0f4` (the help screen, near miss of exactly the game's size): 67 aligned diffs, five
   block-shape causes in its `differs:` note.
5. The two-module experiment for QUESTIONS 6: the palette pair (and ideally `readdat`) from a second
   OMF module so the calls survive `-oe`; that makes `fn_1c068`/`fn_1c0ac` matches and takes 11
   bytes off each of the 24 `readdat` sites in fn_17928/fn_1832c.
6. Two call-shape traps left (`v88bb2` is now `short`, which fixed 0x1d120): `f375cd`'s aux
   `parm [edx]` forces mov+movzwl over a 16-bit load (0x19506, 0x1ce57; `pal_a short` fixes both but
   costs a match), and `telldat() + datsize() - 0x300` accumulates in the wrong register (0x194c8).
7. Inventory: archives and file formats (WACKY.DAT), strings - the `42.GAM`/`20.GAM` dat records
   (`v7eca0` 1/2/3, 0x34463-0x344d4, `v88bd6` = `datfind`'s index), the "BSBCS.PCX" record, and who
   first writes `tbl77758`.
8. Get the original running under the harness; record the boot's segments or LE deltas and a first
   save state in `captures/`. Cheap checks: all eight cars selectable off-net when registered
   (`v7e574 == 0`), and a memory read of a `tbl77758` record (what the +6 stamp holds, LIKELY).
9. Copy this session's generic traps (GOTCHAS: ternary-vs-`if`/`else` stores, the local slot map,
    the frame-sized array, the `short`/`int` slot lever) into `../DREA/reference/DOS.md` - BLOCKED:
    the overnight policy blocks writes outside this repo.

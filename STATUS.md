# STATUS

Short: where the project stands, and what is next. History is in git; do not
keep a session diary here.

## State

- Executables identified: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, options `-s -3r -oailrt`
  (`-oneatx` used; findings/executables.md). Code layout: one C module, an asm block, libraries incl. the
  Apogee Sound System.
- Toolchain: Watcom 9.5/9.5a/9.5b in `.work/toolchains/inst` (not committed). Approach: matching
  rebuild (../DREA METHOD.md 9), then a translated port (10); harness and port not started.
- Classes/modes (findings/classes.md): `v88bb2` = 1 AMATEUR ... 5 KID MODE, `classes_menu` 0x19eac is
  that screen, `fn_19db8` sets the class line `cls_name`, `v7ece0` and the `tbl7e730` index `v88b84`;
  `laps_menu` 0x1a148 (byte-identical) sets `nlaps` to 6/8/10 - 3 in KID MODE, 6 for TIME TRIAL.
- Car select (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b, one 6861-byte C function, uses
  `ents[0..7]` as the eight car icons plus **`ents[8]` as the selection box** (0x73998: `ents` is 9
  records here) and ends with the class/lap/engine/zone menus. `v7d338[car]` is the per-car lock
  (shareware locks cars 0,1,5,6; `v7e574` CONFIRMED the shareware flag, 0x34463-0x344d4).
- Race setup (findings/game-loop.md, both near misses): `fn_1be3c` 0x1be3c builds the start grid
  `v70720[0..7]` (a permutation of 0..7, `myplayer` first and `v88c6a` second in 2p, the rest
  `rand()`-drawn or counting up under splitscreen); `fn_1bf00` sets `players[i].f64` and `.face`.
- Video (findings/video.md): `setpalette` 0x36f14 is **not** a palette op - asm `rep movsb` of
  0x3e80 bytes from A0000+`pal_b` to A0000+`pal_c`, so `pal_a`-`pal_d` are screen page offsets
  (48000 = the fourth page). Its two flippers `fn_1c068`/`fn_1c0ac` rebuild byte-identical but stay
  `extern` here: defining them lets `-oe` inline them and lose `net_end` 0x110d4 (GOTCHAS).

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1bf7d, 84 functions byte-identical and
   35 near misses (`src/check.sh`; near misses are marked `differs:`, and the register/scheduling ones
   are QUESTIONS.md 2, 3). Convention arg1=EAX arg2=EDX arg3=EBX arg4=ECX; stack args pushed by the
   caller and popped by the callee (`ret 4`, findings/menus.md Traps); struct player pack(1) 0x68,
   struct face 0x98. `src/ww.c` is ~6.1 K lines: grep it. Functions after the data-file block
   (0x14128-0x14260) go at the end of the file.
2. Next functions in address order: `fn_1c0f4` 0x1c0f4 (1396 bytes, the F1 controls screen, already
   prototyped and called from `cars_menu` at 0x1b2dd/0x1b415), then 0x1c66c, 0x1c88c, 0x1c968,
   0x1c994, 0x1cad0. If 0x1c0f4 is too big for one pass, do 0x1c66c-0x1c994 first and put it back.
3. Try the two-module experiment for QUESTIONS 6 (the palette pair - and ideally `readdat` - from a
   second OMF module, so the calls survive `-oe`). If it works `fn_1c068`/`fn_1c0ac` become matches
   and the 24 `readdat` sites stop adding 11 bytes each in fn_17928/fn_1832c.
4. Fix the two known call-shape traps before the menus are retried: the `f375cd` aux `parm [edx]`
   forces mov+movzwl where the game does a 16-bit partial load (0x19506), and `telldat() + datsize()
   - 0x300` accumulates in the wrong register (0x194c8).
5. The two store/register near misses: classes_menu's `v7edb0` store schedule (QUESTIONS 3) and
   fn_19db8's register home (QUESTIONS 2). Probe in batch: `tools/wc/variants.py WW.OBJ ww 0x19eac
   0x1a147 name...` scores ~30 shapes in one 4 s compile. `cars_menu`'s 27-slot permutation
   (QUESTIONS 5) needs the same tool: `tools/wc/objdiff.py ww .work/src/WW.OBJ cars_menu`.
6. Inventory: archives and file formats (WACKY.DAT), strings. What are the `42.GAM` / `20.GAM` dat
   records (`v7eca0` = 1/2/3, 0x34463-0x344d4, `v88bd6` = `datfind`'s index)? What dat record is
   "BSBCS.PCX" / "(B)SBn.PCX" (findings/menus.md), and what does `showlines` read?
7. Get the original running under the harness; record the boot's segments or LE deltas and a first
   save state in `captures/`. Cheap check: registered release should have all eight cars selectable
   off-net (`v7e574 == 0`) and "PLEASE READ ORDERING INFO" unreachable off-net.
8. Copy this session's generic traps (GOTCHAS: `-oe` inlines anything you define; DOSBox 8.3 file
   names) plus the earlier twelve into `../DREA/reference/DOS.md` - BLOCKED: the overnight policy
   blocks writes outside this repo.

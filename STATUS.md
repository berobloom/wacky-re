# STATUS

Short: where the project stands, and what is next. History is in git; do not
keep a session diary here.

## State

- Executables identified: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, options `-s -3r -oailrt`
  (`-oneatx` used; findings/executables.md). Code layout: one C module, an asm block, libraries incl. the
  Apogee Sound System.
- Toolchain: Watcom 9.5, 9.5a, 9.5b installed in `.work/toolchains/inst` (not committed).
- Approach: matching rebuild (../DREA METHOD.md section 9), then a translated port (section 10).
  Harness: not started. Port: not started.
- Classes/modes found (findings/classes.md): `v88bb2` = 1 AMATEUR, 2 PRO, 3 CHAMPION, 4 TIME TRIAL,
  5 KID MODE; `classes_menu` 0x19eac is that screen; `fn_19db8` 0x19db8 sets the class line `cls_name`,
  `v7ece0` and the `tbl7e730` index `v88b84` from it. `laps_menu` 0x1a148 (rebuilt byte-identical):
  SIX/EIGHT/TEN LAP RACE rows into `nlaps` 6/8/10, 3 laps when the KID MODE engines are locked
  (`v7edb0`), 6 for a TIME TRIAL outside splitscreen.
- Car select mapped and rebuilt (findings/car_select.md): `cars_menu` 0x1a380-0x1be3b is one
  6861-byte C function (`sub esp,0x17c` = 3 `char[0x50]` buffers, 2 `short[8]` arrays at 0xf0/0x100,
  27 dword locals); it uses `ents[0..7]` as the eight car icons plus **`ents[8]` as the selection
  box** (0x73998, so `ents` is 9 records here), the cursor as a grid point resolved by `find12`,
  the box sprite pair at 0x6e5b0, and ends with the class/lap/engine/zone menus.
  `v7d338[car]` is the per-car lock (shareware locks cars 0,1,5,6; `v7e574` is CONFIRMED the shareware
  flag, 0x34463-0x344d4); the 0x20 bytes at 0x1a360 are two jump tables, not padding.

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1be3b, 84 functions
   byte-identical and 33 near misses (`src/check.sh`; near misses are marked `differs:` in the source, and the
   register/scheduling ones are QUESTIONS.md 2, 3). Layout notes: struct player pack(1), struct face 0x98,
   calling convention arg1=EAX arg2=EDX arg3=EBX arg4=ECX (stack args pushed by the caller and popped by the callee, `ret 4` - see findings/menus.md Traps). `src/ww.c` is ~6 K lines: grep, don't read whole. Functions after the data-file block (0x14128-0x14260) go at the end of the file.
2. Finish `cars_menu` (near miss at +8, 6562 vs 6861 bytes): the statements are in the game's order
   and 71 % of the rows agree with the esp disps masked, so what is left is the scalar slot
   permutation (QUESTIONS.md 5, probe recipe there), our frame being 4 bytes short because the game
   spills one local we keep in a register. Listing: `tools/wc/objdiff.py ww .work/src/WW.OBJ
   cars_menu`. The functions after it start at 0x1be3c, 0x1bf00, 0x1bf80 and 0x1bf94.
3. The two store/register near misses: classes_menu's `v7edb0` store schedule (QUESTIONS 3) and
   fn_19db8's register home (QUESTIONS 2, the home follows the position of the first definition).
   Batch the probes: `tools/wc/variants.py WW.OBJ ww 0x19eac 0x1a147 name...` scores ~30 shapes in
   one 4 s compile (used successfully on `laps_menu`: 58 instruction diffs as an if-chain, 1 as a
   `switch`, 0 after re-shaping the TIME TRIAL test - GOTCHAS).
4. Fix the two known call-shape traps before the menus are retried: the `f375cd` aux `parm [edx]`
   forces mov+movzwl where the game does a 16-bit partial load (0x19506), and
   `telldat() + datsize() - 0x300` accumulates in the wrong register (0x194c8).
5. Inventory: archives and file formats (WACKY.DAT), strings. What are the `42.GAM` / `20.GAM` dat
   records (their presence is the release flavour, `v7eca0` = 1/2/3, 0x34463-0x344d4, and
   `v88bd6` = `datfind`'s index)? Also find what dat record "BSBCS.PCX" / "(B)SBn.PCX" is
   (findings/menus.md), and what `showlines` reads (the 0x7c854 line array vs `netstats`).
6. Get the original running under the harness; record the boot's segments or
   LE deltas and a first save state in `captures/`. A cheap first check there: in the registered
   release all eight cars should be selectable off-net (`v7e574 == 0`), and the "PLEASE READ ORDERING
   INFO" message should be unreachable off-net.
7. Copy this session's generic Watcom trap (GOTCHAS: switch jump tables sit in the gap *before* the
   function, so a gap is not padding - `jmp dword ptr cs:[reg*4+disp]` is the tell), plus the earlier
   twelve, into `../DREA/reference/DOS.md` - BLOCKED: the overnight policy blocks writes outside this repo.

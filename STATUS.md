# STATUS

Short: where the project stands, and what is next. History is in git; do not
keep a session diary here.

## State

- Executables identified: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, options `-s -3r -oailrt`
  (`-oneatx` used; findings/executables.md). Code layout: one C module, an asm block, libraries incl. the
  Apogee Sound System.
- Toolchain: Watcom 9.5, 9.5a, 9.5b installed in `.work/toolchains/inst` (not committed).
- Approach: matching rebuild (../DREA METHOD.md section 9), then a translated port (section 10).
- Harness: not started. Port: not started.
- Classes/modes found (findings/classes.md): `v88bb2` = 1 AMATEUR, 2 PRO, 3 CHAMPION, 4 TIME TRIAL,
  5 KID MODE; `classes_menu` 0x19eac is that screen; `fn_19db8` 0x19db8 sets the class line `cls_name`,
  `v7ece0` and the `tbl7e730` index `v88b84` from it. `laps_menu` 0x1a148 (rebuilt byte-identical) is
  the next screen of the chain: SIX/EIGHT/TEN LAP RACE rows into `nlaps` 6/8/10, 3 laps when the KID
  MODE engines are locked (`v7edb0`), 6 for a TIME TRIAL outside splitscreen; `cars_menu` 0x1a380
  calls `classes_menu`, `laps_menu` and `engines_menu` in a row, each guarded by `v88b9e == 0`.

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x1a35e, 84 functions
   byte-identical and 32 near misses (`src/check.sh`; near misses are marked `differs:` in the source, and the
   register/scheduling ones are QUESTIONS.md 2, 3). Layout notes: struct player pack(1), struct face 0x98,
   calling convention arg1=EAX arg2=EDX arg3=EBX arg4=ECX (stack args pushed by the caller and popped by the callee, `ret 4` - see findings/menus.md Traps). `src/ww.c` is ~5.3 K lines: grep, don't read whole.
   Functions after the data-file block (0x14128-0x14260) go at the end of the file.
   `tools/wc/starts.py ww LO HI` lists function starts; 0x1a188, 0x1a23c and 0x1a264 are jump
   targets inside `laps_menu`, not functions, and 0x1a360 is padding. Next in address order:
   **0x1a380 `cars_menu`** (the "CHOOSE VEHICLE" screen: `pal_a`, the PLAYER ONE/TWO labels,
   `ctl_label` called at 0x1a437, a 0x140-wide sprite row loop, and the three menu calls at
   0x1bd3c/0x1bd4b/0x1bd5f); it runs past 0x1a800, so expect more than one iteration.
2. `fn_1a2f8` (matched this session) clears `v7d338[0..7]` and sets `[0] [1] [5] [6]` to 1: what
   those flags are is QUESTIONS.md 4, and xref of `v7d338` +0x14/+0x18 answers it once the entity
   code is ported.
3. The two store/register near misses: classes_menu's `v7edb0` store schedule (QUESTIONS 3) and
   fn_19db8's register home (QUESTIONS 2, the home follows the position of the first definition).
   Batch the probes: `tools/wc/variants.py WW.OBJ ww 0x19eac 0x1a147 name...` scores ~30 shapes in
   one 4 s compile (used successfully on `laps_menu`: 58 instruction diffs as an if-chain, 1 as a
   `switch`, 0 after re-shaping the TIME TRIAL test - GOTCHAS).
4. Fix the two known call-shape traps before the menus are retried: the `f375cd` aux `parm [edx]`
   forces mov+movzwl where the game does a 16-bit partial load (0x19506), and
   `telldat() + datsize() - 0x300` accumulates in the wrong register (0x194c8).
5. Inventory: archives and file formats (WACKY.DAT), strings. Find what dat record "BSBCS.PCX" /
   "(B)SBn.PCX" is (findings/menus.md), and what `showlines` reads (the 0x7c854 line array vs
   `netstats`).
6. Get the original running under the harness; record the boot's segments or
   LE deltas and a first save state in `captures/`.
7. Copy this session's three generic Watcom traps (GOTCHAS: a 3-case `switch` on a `short` is a
   compare tree, not a jump table; two identical store blocks merge only in some C shapes; a call
   target can be an interior label of a clib module, `libmatch.py -l`), plus the earlier eleven, into
   `../DREA/reference/DOS.md` - BLOCKED: the overnight policy blocks writes outside this repository.

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
  5 KID MODE; `classes_menu` 0x19eac is that screen (5 rows, 4 in a net game; two-player/net-voice
  games go straight to TIME TRIAL; splitscreen 2 offers only the first three rows; KID MODE shows
  nine help lines and then forces AMATEUR + `v7edb0 = 1`); `fn_19db8` 0x19db8 sets the class line
  `cls_name`, `v7ece0` and the `tbl7e730` index `v88b84` from it.

## Next

Take the first open item when QUEUE.md has none open. Mark items CLOSED or
BLOCKED (with the reason) rather than deleting them.

1. Matching rebuild, in progress: `src/ww.c` covers 0x10260-0x19ea8, 82 functions
   byte-identical and 32 near misses (`src/check.sh`; near misses are marked `differs:` in the source, and the
   register/scheduling ones are QUESTIONS.md 2, 3). Layout notes: struct player pack(1), struct face 0x98,
   calling convention arg1=EAX arg2=EDX arg3=EBX arg4=ECX (stack args pushed by the caller and popped by the callee, `ret 4` - see findings/menus.md Traps). `src/ww.c` is ~5.2 K lines: grep, don't read whole.
   `tools/wc/starts.py ww LO HI` lists function starts (it can also list loop bodies that follow a jmp).
   Functions after the data-file block (0x14128-0x14260) go at the end of the file. Next in address
   order: **0x1a148** (the lap/round part of the same screen: checks twoplayer/netvoices, sets
   `v88b9e`, and at 0x1a178 sets `nlaps` when `v7edb0 != 0`), then 0x1a188, then 0x1a380 `cars_menu`.
2. Probe statement placement with one compile per batch instead of guessing: `tools/wc/variants.py
   WW.OBJ ww 0x19eac 0x1a147 name...` scores ~40 variants of one function in 4 s (see GOTCHAS "Watcom slides
   global stores across an inline strcpy's string-op unit"). Use it for the two store-schedule near
   misses (classes_menu 0x1a05d, fn_19db8's four stores) before re-probing types or -o letters.
3. The register-home rule (QUESTIONS.md 2): the home follows the position of the first definition
   (classes_menu's `nrows`: EDX when defined early, ECX when defined at its use). Use that when
   retrying fn_19db8 (0x19db8: game homes the long-lived local in EDX, ours in EAX) and ctl_label's
   +2 at 0x19d0e; start from the two listings side by side, not from more C shapes.
4. Fix the two known call-shape traps before the menus are retried: the `f375cd` aux `parm [edx]`
   forces mov+movzwl where the game does a 16-bit partial load (0x19506), and
   `telldat() + datsize() - 0x300` accumulates in the wrong register (0x194c8).
5. Inventory: archives and file formats (WACKY.DAT), strings. Find what dat record "BSBCS.PCX" /
   "(B)SBn.PCX" is (findings/menus.md), and what `showlines` reads (the 0x7c854 line array vs
   `netstats`).
6. Get the original running under the harness; record the boot's segments or
   LE deltas and a first save state in `captures/`.
7. Copy this session's four generic Watcom traps (GOTCHAS: stores slide across a strcpy's string-op
   unit; not every placement is reachable; a local's register home follows its first definition; a
   cold `then` block is emitted after the chain) plus the earlier seven into `../DREA/reference/DOS.md`
   - the overnight policy blocks writes outside this repository.

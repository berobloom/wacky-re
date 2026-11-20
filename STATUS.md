# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **207 of 285 functions byte-identical** (`src/matched.baseline`, `src/check.sh`; iter 75 re-ran
  check.sh after the cars_menu behaviour fix: still 207; iter 74: check.sh also
  compiles `tools/link/extra.c`, whose `fn_1c068`/`fn_1c0ac` were always MATCH but never counted; objmatch
  compares rel32 branches between our own functions by target, which made demo_rec/pic_show/fade_out
  MATCH, `tools/wc/test_objmatch.py`), and iter
  66 checked what a MATCH masks inside those 202: all 4556 linker-filled operands point where the
  game's point (`tools/wc/fixsites.py`, 0 disagreements; `test_fixsites.py`). C covers
  all of 0x10260-0x36a4d (no gaps left, iter 56c); 0x36a4e on is the
  hand-written asm module.
- Near misses: `src/nearmiss.txt` is the work list (ranked by `tools/wc/nearmiss.sh`; status
  column MATCH / SKIP: cause / LAYOUT / empty = open). Each function's causes and tried spellings
  are in its `differs:` note in `src/ww.c`.
- What works and what does not: GOTCHAS.md "This project's traps" - *Reading a near-miss diff*,
  *Levers that produced MATCHes*, *Dead ends*. Do not copy levers into this file.
- Findings, evidence at the links: serial (findings/serial.md) rings 0x84b00/0x80afa, wrap 0x4000;
  classes (findings/classes.md) `v88bb2` is 1 AMATEUR..5 KID MODE **signed**; car select
  (findings/car_select.md) `ents[8]` 0x73998 is the box; scores (findings/scores.md) `tbl77758`;
  disc files (findings/formats.md) WACKY.DAT = 577 records of 22 bytes, offsets from 2, 42 sets of
  per-track files.

## Next

1. `menu` 0x155c0 and `race_results` 0x2bff0: each prints SAME at full return with
   `TICK=8 SEED=0x88cce=1:b` (iter 75) - run each control (`NEW=wworig MAP=.work/link/WWORIG.MAP`,
   same seed) and write the two `SEEDED:` lines.
2. `chat` 0x14d00 (instruction cap, it sits at 0x150ec `cmp eax,0x18`): `FN=chat N=60000 TICK=8
   .work/venv/bin/python tools/wc/hotfn.py` names the loop and the global; seed that.
3. `fn_193a8` 0x193a8 still crashes at 0x182fd `mov cl, [edx]` (write_prot 0xfffffffd): seed that
   pointer as `kart_draw`'s was - the block must be zero in both images (findings/game-loop.md Traps).
4. The functions ending in a key press+release pair - `fn_1c0f4`, `race_records_menu`, `fn_1832c`,
   `main_menu` (iter 78: its only return is jt_1f824 slot 8 -> 0x16978, needs selection 9,
   findings/menus.md) - need a difffn knob re-asserting a global at every stubbed call (a press set
   only after the mode-0 `wait_release` returned); cheapest test: `RESEED=`, try `fn_1c0f4`.
5. The still-open CALLCAP set - `screenshot`, `enter_name`, `trial_book`, `demo_mode`, `fn_17928`,
   `fn_1be3c`, `fn_1d704`, `race_run`, `fn_1c0f4` - one run each with `TICK=8 CALLS=3000
   SEED=0x88cce=1:b`, plus the volume_menu recipe: seed the *branch test* of a `ticks = 0; while
   (ticks <= N)` wait (0x7ed4c for 0x1e7af), not `ticks`.
6. After every `tools/link/build.sh`: `tools/link/fixcensus.py` must print `0 unexplained` (GOTCHAS,
   first trap). After a C change, also re-run the behaviour sweep (`awk '$2 ~ /^0x/ && $4 !~ /^MATCH/
   {print $3}' src/nearmiss.txt | while read n; do .work/venv/bin/python tools/wc/difffn.py $n; done`)
   and compare the DIFFs against the recorded verdicts in `src/nearmiss.txt`; the known uncapped DIFFs
   are the dead `v7d7f4`/`v7d874` pointers (findings/game-loop.md).
7. Register homes (QUESTIONS 2): most of the 78 near misses differ only in which register or stack
   slot 9.5b picks; permute.py runs of 15 min gave 0-1 rows (iter 74). Not an overnight item.

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation (except where
  the spilled locals are the whole frame - see the `volatile` array lever), a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

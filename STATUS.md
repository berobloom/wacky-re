# STATUS

Short: where the project stands, and what is next. History is in git; no diary.

## State

- Executables: WW.EXE 1.1, bound DOS/4GW LE, Watcom C/C++32 **9.5b**, `-s -3r -oailrt` (`-oneatx`
  used); one C module, an asm block, libraries (findings/executables.md).
- **198 of 281 compared functions byte-identical** (`src/matched.baseline`, `src/check.sh`). C covers
  0x10260-0x36a4d bar the gaps 0x1bf7e-0x1c0f3 (a fade-out) and 0x1f824-0x1f857; 0x36a4e on is the
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

1. Near misses: no open status in `src/nearmiss.txt` (QUEUE.md's only item is the unfilled
   `Q1 <short title>` template - not actionable, iters 46-51); every remaining line's cause is a
   class in GOTCHAS "Dead ends". Take one only with an idea its `differs:` note does not list.
   nmrefs after every change; `src/check.sh` count +1 before `src/matched.baseline` goes up.
2. The per-track payloads (offline, no harness): `tools/le/datdump.py 1.SIN 1.POS 1.PAR 1.M`
   against the byte-identical `track_load` 0x279e8 and `data_load` 0x28180 - sizes 1296, 8194,
   25600, 4096; lay the records out field by field in findings/formats.md.
3. `SETUP.EXE` (MZ 16-bit, never analysed; `tools/mz.py` loads it): what it writes into
   `WACKY.CFG` - it is the only thing that can explain the zeroed version/`+0x0a`/`+0x14`/`+0x18`
   and 24 records, and the 12 key words of `cfg7b6d8` (QUESTIONS 16/17).
4. `savedir` 0x7c158: `""` unless `APOGEECD` is set (`src/ww.c:19266`, `19277`), so the save files
   are `<cwd>WACKY<n>.HI`/`.TIM` by default and the shipped `WACKY.CFG` picks slot 1
   (`cfg7ee1c` +0x3e); what is left is what `records_load`'s create-from-DAT path writes on a
   fresh install (`src/ww.c:19069-19110`, the rebuild MATCHes) and how `duckfame_load` 0x35534
   finds `WACKY.DTT` there.
4. The two C gaps: 0x1f824-0x1f857 (small, between two rebuilt functions) and the fade-out
   0x1bf7e-0x1c0f3 with its out-of-line tail at 0x32cc1-0x32cf7 (`pic_show`'s padding diff).
5. After a MATCH: rerun `tools/progress.py` (PROGRESS.md, per part and per subsystem of
   `analysis/ww_subsystems.txt`; src/ww.c stays one file); docs/media pictures come from the
   original (DOSBox-X states `mainmenu`, `trackpick` in .work/dos/states; accelerate is Alt).

## BLOCKED

- The dead-end classes in GOTCHAS ("Dead ends: SKIP at once"): stack-slot permutation, a global
  promoted across a loop, the EAX/pushed-register coin flip, straight-line scheduling, folded
  address constants. QUESTIONS.md 1, 2, 3, 9, 10.

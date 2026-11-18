# findings/ INDEX: read this first, then open ONE file

How to work: `../METHOD.md`. Status: **CLOSED** = confirmed and pinned by a
check, do not re-derive; **OPEN** = work remains (say what).

| Question | File | Status |
|---|---|---|
| Executables: format, size, sha256, entry, compiler, source file names, calling convention | `executables.md` | OPEN (compiler pinned to 9.5b; options open) |
| File formats and archives on the disc | `formats.md` | OPEN (WACKY.DAT's container and the 577-entry census CONFIRMED; PCX blob layout, `.GAM` text (field list CONFIRMED: 25 or 28 lines), `WACKY.DTT` records pinned; the `WACKY.ING`/`WACKY.CFG` config block layout CONFIRMED (0x42 header + 24 records of 0x2e, the records never read by the exe); the per-track payloads CONFIRMED field-wise - `.POS` 64x64 zone map + `ncells`, `.SIN` 108 x 12-byte tile surface classes, `.M` 64x64 tile indices, `.RD`/`.SPW` count + records, `.PAR` 25600-byte blit blob (its pixel geometry open); the `cfg7b6d8`/`cfg77128` fields open; the `WACKY1.HI`/`.TIM` names are pinned and the shipped copies equal the DAT defaults `WAC1.HTT`/`WAC1.OTT`; the twelve zone-less slots are six duplicate art pairs and only tracks 1..30 can ever be loaded) |
| Memory map: segments / LE objects, globals, tables | `memory.md` | OPEN |
| Race setup, main loop, frame timing; the race-start helpers `fn_1be3c` (start grid `v70720[]`) and `fn_1bf00` (per-player rows); the hit tests `kart_hit`/`shot_hit` and `karts[]` 0x68410 = `cars[]` - 0x10; the surface probe `f37b04` 0x37b04 | `game-loop.md` | OPEN (the two helpers mapped and rebuilt as near misses; the loop itself not; `f37b04`'s tile -> mask byte -> `.SIN` chain CONFIRMED by disasm) |
| Input: keyboard handler, key table, mouse | `input.md` | OPEN |
| Video: mode, palette, blitter, back buffer, present; `setpalette` 0x36f14 = a 0x3e80-byte screen page copy, `pal_a`-`pal_d` are page offsets, the masked blit `fn_36f55` 0x36f55 | `video.md` | OPEN (the page-flip pair 0x1c068/0x1c0ac CONFIRMED byte-identical; mode `0x13` set at `src/ww.c:19453`; buffer open) |
| Menus: the bonus race / bonus zone order screens, palette flash, the F1 controls screen (`fn_1c0f4` 0x1c0f4, twelve 80-byte frame slots, names from 84-byte records at 0x77432) | `menus.md` | OPEN |
| Sound: driver, tracks, samples, music | `sound.md` | OPEN |
| Classes and game modes: the class menu, the per-class line/value/index, the lap-count menu | `classes.md` | OPEN (menus 0x19eac rebuilt as a near miss, 0x1a148 and 0x1a2f8 byte-identical; the users of the globals after them are not) |
| Score / record tables: `fn_1cad0` 0x1cad0 selects a 46-byte record of `tbl77758` (rows of 0x450) into `v7d8a8`, `fn_1c994` 0x1c994 builds the stamp string, `scores_screen` 0x1ce14 inserts the run's score among the group's first four records, `enter_name` 0x1cb44 types the name, the record-book menus `race_records_menu` 0x1d13c / `trial_records_menu` 0x1d340, the record table `fn_1d704` 0x1d704, `dos_gettime` 0x43164 INT 21h/2Ah | `scores.md` | OPEN (seven functions rebuilt - 0x1cad0 and `trial_records_menu` byte-identical, `race_records_menu` 1 block, `fn_1d704` one spill, 0x1c994 4 diffs, `enter_name` 21 slot diffs, `scores_screen` 97 slot/register diffs; rows 1-6 of `tbl77758` come from `<savedir>WACKY<n>.HI` in `records_load` 0x357ac, row 0 is slack, and the save slot `v7ee5a` is the engine choice (it is `cfg7ee1c` +0x3e, a field of the block `WACKY.CFG` is read into - findings/formats.md)) |
| Serial link: the UART handlers `com_isr` 0x106c8 / `com_isr_fifo` 0x10810, the 16 KB `rxbuf`/`txbuf` rings and their counters | `serial.md` | OPEN (`com_isr` byte-identical; `com_isr_fifo` and the `com_getc`/`com_putc` side are near misses, nothing run) |
| Car select: `cars_menu` 0x1a380, the nine `ents[]` records (8 icons + the selection box), the per-car lock, the tail menu chain | `car_select.md` | OPEN (mapped, rebuilt as a near miss; the scalar slot permutation is the open part) |

Add a row per subsystem. Keep each file to what is true now; history lives in
git. Template: `_TEMPLATE.md`.

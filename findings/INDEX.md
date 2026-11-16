# findings/ INDEX: read this first, then open ONE file

How to work: `../METHOD.md`. Status: **CLOSED** = confirmed and pinned by a
check, do not re-derive; **OPEN** = work remains (say what).

| Question | File | Status |
|---|---|---|
| Executables: format, size, sha256, entry, compiler, source file names, calling convention | `executables.md` | OPEN (compiler pinned to 9.5b; options open) |
| File formats and archives on the disc | `formats.md` | OPEN |
| Memory map: segments / LE objects, globals, tables | `memory.md` | OPEN |
| Startup chain, main loop, frame timing; the race-start helpers `fn_1be3c` (start grid `v70720[]`) and `fn_1bf00` (per-player rows) | `game-loop.md` | OPEN (the two helpers mapped and rebuilt as near misses; the loop itself not) |
| Input: keyboard handler, key table, mouse | `input.md` | OPEN |
| Video: mode, palette, blitter, back buffer, present; `setpalette` 0x36f14 = a 0x3e80-byte screen page copy, `pal_a`-`pal_d` are page offsets | `video.md` | OPEN (the page-flip pair 0x1c068/0x1c0ac CONFIRMED byte-identical; mode/buffer open) |
| Menus: the bonus race / bonus zone order screens, palette flash, the F1 controls screen (`fn_1c0f4` 0x1c0f4, twelve 80-byte frame slots, names from 84-byte records at 0x77432) | `menus.md` | OPEN |
| Sound: driver, tracks, samples, music | `sound.md` | OPEN |
| Classes and game modes: the class menu, the per-class line/value/index, the lap-count menu | `classes.md` | OPEN (menus 0x19eac rebuilt as a near miss, 0x1a148 and 0x1a2f8 byte-identical; the users of the globals after them are not) |
| Score / record tables: `fn_1cad0` 0x1cad0 selects a 46-byte record of `tbl77758` (rows of 0x450) into `v7d8a8`, `fn_1c994` 0x1c994 builds the stamp string, `scores_screen` 0x1ce14 inserts the run's score among the group's first four records, `enter_name` 0x1cb44 types the name, `dos_gettime` 0x43164 INT 21h/2Ah | `scores.md` | OPEN (four helpers rebuilt - 0x1cad0 byte-identical, 0x1c994 4 diffs, `enter_name` 21 slot diffs, `scores_screen` 97 slot/register diffs; the code that first fills `tbl77758` is still missing) |
| Car select: `cars_menu` 0x1a380, the nine `ents[]` records (8 icons + the selection box), the per-car lock, the tail menu chain | `car_select.md` | OPEN (mapped, rebuilt as a near miss; the scalar slot permutation is the open part) |

Add a row per subsystem. Keep each file to what is true now; history lives in
git. Template: `_TEMPLATE.md`.

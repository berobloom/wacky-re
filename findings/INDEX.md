# findings/ INDEX: read this first, then open ONE file

How to work: `../METHOD.md`. Status: **CLOSED** = confirmed and pinned by a
check, do not re-derive; **OPEN** = work remains (say what).

| Question | File | Status |
|---|---|---|
| Executables: format, size, sha256, entry, compiler, source file names, calling convention | `executables.md` | OPEN (compiler pinned to 9.5b; options open) |
| File formats and archives on the disc | `formats.md` | OPEN |
| Memory map: segments / LE objects, globals, tables | `memory.md` | OPEN |
| Startup chain, main loop, frame timing | `game-loop.md` | OPEN |
| Input: keyboard handler, key table, mouse | `input.md` | OPEN |
| Video: mode, palette, blitter, back buffer, present | `video.md` | OPEN |
| Menus: the bonus race / bonus zone order screens, palette flash | `menus.md` | OPEN |
| Sound: driver, tracks, samples, music | `sound.md` | OPEN |

Add a row per subsystem. Keep each file to what is true now; history lives in
git. Template: `_TEMPLATE.md`.

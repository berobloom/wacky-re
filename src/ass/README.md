# The Apogee Sound System, Wacky Wheels' revision

Wacky Wheels links a revision of the Apogee Sound System (ASS) older than any released LIB
(findings/executables.md "Sound library"). This directory is that library being reconstructed.

- **Base**: the C sources and headers of
  [gamesrc-ver-recreation/audiolib](https://github.com/gamesrc-ver-recreation/audiolib) at
  `c363124d376d373441a21b3304eb44d32c0a69f7`, which rebuild ASS 1.04-1.12 from the GPL release
  (Copyright 1994-1995 Apogee Software; GNU GPL version 2 or later, `COPYING`). Its
  `LIBVER_ASSREV` macro selects the revision: 19950518 is 1.04, later dates the later releases.
- **Wacky Wheels' revision is `LIBVER_ASSREV` 19941101** (a placeholder below 1.04; the real date is
  unknown). Every change made here for it sits under `#if (LIBVER_ASSREV < 19950518L)`, so the 1.04
  build is untouched: `tools/ass/asscheck.py --lib` rebuilds 1.04 and must stay identical to the
  original 1.04 LIB, module by module.
- **Build**: `tools/ass/build.sh` (one DOSBox run, AUDIO.MAK's 1.04 production flags). The compiler
  is per module: Watcom 10.0a, except USER, GUS and PITCH (the Watcom 10.0 prerelease: the game's
  objects predate the switch to 10.0a);
  10.0b for the 1.04 rebuild.
- **Check**: `tools/ass/asscheck.py` compares every function with the game at its address in
  analysis/ww_names.txt (`tools/wc/objmatch.py`): `ASS: N of M named functions byte-identical`.
- GUSWAVE.C holds Wacky Wheels' GUSWAVE as one alternative block (it differs in every function).
- `tools/ass/portnames.py MODULE` names a whole-identical module's functions and data in
  analysis/ww_names.txt (the "ASS port" block).
- Not built: the assembler mixers (`MV_MIX*.ASM`, need TASM) and the GF1 library (`gf1_osf.lib`,
  linked from the original LIB; Gravis's 1993 GF1OS source is an older revision).

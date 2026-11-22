# Nuked OPL3 1.8

The YMF262 (OPL3) emulator by Nuke.YKT, LGPL-2.1-or-later (the notice heads `opl3.c` and `opl3.h`;
license text: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html). The port drives it as the
AdLib / Sound Blaster OPL2 at 0x388 that the game's FM song player writes (port/src/audio.c).

Copied from DOSBox-X (the harness's emulator: dos-re-suite's pinned commit
2917cb31e00a9d0a935060ac9186c1a7885da0fd, `src/hardware/nukedopl.cpp` / `nukedopl.h`), which pulled
upstream commit 730f8c238a152cb3314e46919789a666c9d81d82 of https://github.com/nukeykt/Nuked-OPL3.
Changes: the files take upstream's names again (`opl3.c`, `opl3.h`) and `opl3.c` includes `opl3.h`.
Nothing else is edited.

#!/bin/sh
# Build the port: port/build.sh  ->  .work/port/wacky (32-bit, SDL2)
#
# src/ww.c is compiled unchanged after port/tools/prep.py, with Watcom 9.5b's defaults (struct
# packing 1, unsigned char: findings/compiler.md) and -O0 (the game's busy waits on interrupt-
# driven globals must re-read memory). port/gen/syms.ld puts its extern globals at their game
# addresses. Needs gcc-multilib; SDL2 from the installed i386 runtime library (the 64-bit headers
# work for both: _real_SDL_config.h picks the pointer size).
set -eu
cd "$(dirname "$0")/.."
out=.work/port
mkdir -p "$out/obj"
CC="gcc -m32"
SDLINC=/usr/include/x86_64-linux-gnu
SDLLIB=/usr/lib/i386-linux-gnu/libSDL2-2.0.so.0
GAME="-O0 -g -w -fno-pie -fno-stack-protector -fno-strict-aliasing -fwrapv -funsigned-char -fpack-struct=1 \
      -fno-builtin -Iport/include -include port_clib.h"
RT="-O1 -g -Wall -Wextra -Wno-unused-parameter -fno-pie -fno-stack-protector -fno-strict-aliasing -Iport/include -I$SDLINC"

python3 port/tools/prep.py src/ww.c "$out/ww.c"
$CC $GAME -Dmain=game_main -c "$out/ww.c" -o "$out/obj/ww.o"
$CC $GAME -c tools/link/extra.c -o "$out/obj/extra.o"
for f in image vga machine clib sound glue main asmstub; do
    $CC $RT -c port/src/$f.c -o "$out/obj/$f.o"
done
$CC -no-pie -g -o "$out/wacky" "$out"/obj/*.o port/gen/syms.ld "$SDLLIB" -lpthread -lm
echo "$out/wacky"

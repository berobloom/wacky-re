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
python3 port/tools/asm2c.py src/wwasm.asm "$out/wwasm.c"
$CC $GAME -Dmain=game_main -c "$out/ww.c" -o "$out/obj/ww.o"
$CC $GAME -c tools/link/extra.c -o "$out/obj/extra.o"
for f in image vga machine clib wrand sound glue main cpu audio; do
    $CC $RT -Iport/third_party/nukedopl -c port/src/$f.c -o "$out/obj/$f.o"
done
# Nuked OPL3 (port/third_party/nukedopl/README.md): C, LGPL
$CC -O2 -g -w -fno-pie -x c -Iport/third_party/nukedopl -c port/third_party/nukedopl/opl3.c -o "$out/obj/opl3.o"
$CC $RT -Wno-unused-label -Iport/src -c "$out/wwasm.c" -o "$out/obj/wwasm.o"
# the game's FM song player, rebuilt from the binary (src/ass/FMSONG.C, FMCHIP.C; findings/executables.md):
# it includes its headers by lowercase names, so give it a folded include directory
inc="$out/assinc"
mkdir -p "$inc"
for h in src/ass/*.H; do b=$(basename "$h" .H | tr A-Z a-z); ln -sf "$PWD/$h" "$inc/$b.h"; done
rm -f "$inc/interrup.h"                       # port/include/interrupt.h stands in for it
ASS="-O1 -g -w -fno-pie -fno-stack-protector -fno-strict-aliasing -funsigned-char -Iport/include -I$inc -DLIBVER_ASSREV=19941101L"
for m in FMSONG FMCHIP FX_MAN MULTIVOC PITCH LL_MAN USRHOOKS; do
    if grep -q "'$m.C'" port/tools/prep.py; then
        python3 port/tools/prep.py src/ass/$m.C "$out/ass_$m.c"; srcf="$out/ass_$m.c"
    else
        srcf=src/ass/$m.C
    fi
    $CC $ASS -Isrc/ass -x c -c "$srcf" -o "$out/obj/ass_$m.o"
done
# the port's Sound Blaster under MULTIVOC, and the cards it does not have (they use the ASS headers)
for f in blaster cards; do
    $CC $RT -I"$inc" -c port/src/$f.c -o "$out/obj/$f.o"
done
$CC -no-pie -g -o "$out/wacky" "$out"/obj/*.o port/gen/syms.ld "$SDLLIB" -lpthread -lm
echo "$out/wacky"

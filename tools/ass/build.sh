#!/bin/sh
# Build the Apogee Sound System C modules of src/ass with Watcom C, headless, in one DOSBox run.
#
#   [ASSWC=WC95B] [ASSMODS="TASK_MAN ..."] tools/ass/build.sh [REV]     (ASSMODS: build only these)
#
# REV = LIBVER_ASSREV: default 19941101, Wacky Wheels' revision (src/ass/README.md); 19950518 builds
# ASS 1.04 as the recreation does. Objects go to .work/ass/REV/ (MODULE.OBJ, WCC.OUT = the compiler's
# messages). Flags per module are AUDIO.MAK's 1.04 production flags.
#
# Compiler per module (installs under .work/toolchains/inst, findings/executables.md "Sound library"):
#   REV 19950518: WC100B for all (10.0a + the .b patch; it rebuilds the original 1.04 LIB byte for byte);
#   Wacky Wheels: WC100A, except the modules in WW95B below, whose objects in the game are 9.5b's
#   (the game's make left them from before the 10.0 switch). ASSWC=<install> forces one compiler for
#   all modules and writes to .work/ass/REV-<install>/ instead.
set -eu
cd "$(dirname "$0")/../.."
rev=${1:-19941101}
WW95B="USER GUS"
inst=$PWD/.work/toolchains/inst
out=.work/ass/$rev${ASSWC:+-$ASSWC}
mkdir -p "$out"
for w in WC100B WC100A WC95B; do
    [ -f "$inst/$w/BINB/WCC386.EXE" ] || [ -f "$inst/$w/BIN/WCC386.EXE" ] || { echo "no WCC386.EXE in $inst/$w"; exit 1; }
done
drive() { case $1 in WC100B) echo C;; WC100A) echo F;; WC95B) echo G;; *) echo "unknown compiler $1" >&2; exit 1;; esac; }
std="/zq /w4 /d1 /omaxnet /zp4 /5r"
bat=$out/BUILD.BAT
printf '@echo off\r\n' > "$bat"
all="ADLIBFX AL_MIDI AWE32 BLASTOLD DEBUGIO DMA DPMI FX_MAN GMTIMBRE GUS GUSMIDI GUSWAVE IRQ LL_MAN
     MIDI MPU401 MULTIVOC MUSIC PAS16 PCFX PITCH SNDSCAPE SNDSRC TASK_MAN USER"
mods=${ASSMODS:-$all}
for m in $mods; do
    case $m in
        GUS|GUSMIDI)          f="/zq /w4 /d2" ;;
        MULTIVOC)             f="/zq /w4 /d1 /omaxne /zp4 /5r /zu" ;;
        MIDI|AL_MIDI)         f="$std /zu" ;;
        *)                    f="$std" ;;
    esac
    if [ -n "${ASSWC:-}" ]; then w=$ASSWC
    elif [ "$rev" -ge 19950518 ]; then w=WC100B
    else case " $WW95B " in *" $m "*) w=WC95B;; *) w=WC100A;; esac
    fi
    d=$(drive $w)
    bin=BINB; [ -f "$inst/$w/BINB/WCC386.EXE" ] || bin=BIN
    printf 'set INCLUDE=%s:\\H\r\nset WATCOM=%s:\\\r\n%s:\\%s\\WCC386.EXE %s /i=D:\\ -DLIBVER_ASSREV=%sL D:\\%s.C /fo=E:\\%s.OBJ >> E:\\WCC.OUT\r\n' \
        "$d" "$d" "$d" "$bin" "$f" "$rev" "$m" "$m" >> "$bat"
    rm -f "$out/$m.OBJ"
done
: > "$out/WCC.OUT"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 600 dosbox -noconsole -exit \
    -c "mount c \"$inst/WC100B\"" -c "mount f \"$inst/WC100A\"" -c "mount g \"$inst/WC95B\"" \
    -c "mount d \"$PWD/src/ass\"" -c "mount e \"$PWD/$out\"" \
    -c "set PATH=C:\\BINB;C:\\BIN" -c "set DOS4G=QUIET" \
    -c "e:" -c "call E:\\BUILD.BAT" -c exit >/dev/null 2>&1 || true
n=$(ls "$out"/*.OBJ 2>/dev/null | wc -l)
tr -d '\r' < "$out/WCC.OUT" | grep -E 'Error!' | head -8 || true
want=$(echo $mods | wc -w); got=0
for m in $mods; do [ -f "$out/$m.OBJ" ] && got=$((got + 1)); done
echo "$out: $got of $want modules built"
[ "$got" = "$want" ]

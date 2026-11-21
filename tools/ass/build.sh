#!/bin/sh
# Build the Apogee Sound System C modules of src/ass with Watcom C, headless.
#
#   [ASSWC=WC95B] [ASSMODS="TASK_MAN ..."] tools/ass/build.sh [REV]     (ASSMODS: build only these)
#
# REV = LIBVER_ASSREV: default 19941101, Wacky Wheels' revision (src/ass/README.md); 19950518 builds
# ASS 1.04 as the recreation does. Objects go to .work/ass/REV/ (MODULE.OBJ, WCC.OUT = the compiler's
# messages). Flags per module are AUDIO.MAK's 1.04 production flags.
#
# Compiler (installs under .work/toolchains/inst, findings/executables.md "Sound library"):
#   REV 19950518: WC100B for all (10.0a + the .b patch; it rebuilds the original 1.04 LIB byte for byte);
#   Wacky Wheels: WC100R for all, the Watcom 10.0 release (5/1994). The only copy found has OS/2 and NT
#   hosts but no DOS one, so it runs under Wine (BINNT/wcc386.exe loads the BINB compiler image);
#   the DOS-hosted ones (WC100B, WC100A, WC95B, WC100 = the 3/1994 prerelease) run in one DOSBox run.
#   ASSWC=<install> forces one compiler for all modules and writes to .work/ass/REV-<install>/ instead.
set -eu
cd "$(dirname "$0")/../.."
rev=${1:-19941101}
inst=$PWD/.work/toolchains/inst
out=.work/ass/$rev${ASSWC:+-$ASSWC}
mkdir -p "$out"
drive() { case $1 in WC100B) echo C;; WC100A) echo F;; WC95B) echo G;; WC100) echo H;; *) echo "unknown compiler $1" >&2; exit 1;; esac; }
std="/zq /w4 /d1 /omaxnet /zp4 /5r"
bat=$out/BUILD.BAT
printf '@echo off\r\n' > "$bat"
dos=0
wine=$out/WINE.LST
: > "$wine"
all="ADLIBFX AL_MIDI AWE32 BLASTOLD DEBUGIO DMA DPMI FMCHIP FMSONG FX_MAN GMTIMBRE GUS GUSMIDI GUSWAVE IRQ LL_MAN
     MIDI MPU401 MULTIVOC MUSIC PAS16 PCFX PITCH SNDSCAPE SNDSRC TASK_MAN USER"
mods=${ASSMODS:-$all}
: > "$out/WCC.OUT"
for m in $mods; do
    case $m in
        GUS|GUSMIDI)          f="/zq /w4 /d2"
                              # Wacky Wheels' GUSMIDI: optimised, but packed (no /zp4: its GF1 patch structs are 1-byte aligned)
                              [ "$rev" -lt 19950518 ] && [ "$m" = GUSMIDI ] && f="/zq /w4 /d1 /omaxnet /5r" ;;
        MULTIVOC)             f="/zq /w4 /d1 /omaxne /zp4 /5r /zu"
                              # Wacky Wheels' MULTIVOC: no /zu (its functions use ebp as a register)
                              [ "$rev" -lt 19950518 ] && f="/zq /w4 /d1 /omaxne /zp4 /5r" ;;
        MIDI|AL_MIDI)         f="$std /zu"
                              # Wacky Wheels' MIDI: no /zu (its functions use ebp as a register)
                              [ "$rev" -lt 19950518 ] && [ "$m" = MIDI ] && f="$std" ;;
        *)                    f="$std" ;;
    esac
    if [ -n "${ASSWC:-}" ]; then w=$ASSWC
    elif [ "$rev" -ge 19950518 ]; then w=WC100B
    else w=WC100R
    fi
    rm -f "$out/$m.OBJ"
    if [ "$w" = WC100R ]; then
        echo "$m $f" >> "$wine"
        continue
    fi
    [ -f "$inst/$w/BINB/WCC386.EXE" ] || [ -f "$inst/$w/BIN/WCC386.EXE" ] || { echo "no WCC386.EXE in $inst/$w"; exit 1; }
    d=$(drive $w)
    bin=BINB; [ -f "$inst/$w/BINB/WCC386.EXE" ] || bin=BIN
    printf 'set INCLUDE=%s:\\H\r\nset WATCOM=%s:\\\r\n%s:\\%s\\WCC386.EXE %s /i=D:\\ -DLIBVER_ASSREV=%sL D:\\%s.C /fo=E:\\%s.OBJ >> E:\\WCC.OUT\r\n' \
        "$d" "$d" "$d" "$bin" "$f" "$rev" "$m" "$m" >> "$bat"
    dos=1
done
if [ "$dos" = 1 ]; then
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 600 dosbox -noconsole -exit \
        -c "mount c \"$inst/WC100B\"" -c "mount f \"$inst/WC100A\"" -c "mount g \"$inst/WC95B\"" -c "mount h \"$inst/WC100\"" \
        -c "mount d \"$PWD/src/ass\"" -c "mount e \"$PWD/$out\"" \
        -c "set PATH=C:\\BINB;C:\\BIN" -c "set DOS4G=QUIET" \
        -c "e:" -c "call E:\\BUILD.BAT" -c exit >/dev/null 2>&1 || true
fi
if [ -s "$wine" ]; then
    r=$inst/WC100R
    [ -f "$r/BINNT/wcc386.exe" ] && [ -f "$r/BINB/wcc386.exe" ] || { echo "no BINNT/wcc386.exe + BINB/wcc386.exe in $r"; exit 1; }
    command -v wine >/dev/null || { echo "WC100R needs wine"; exit 1; }
    winpath() { printf 'Z:%s' "$1" | tr / '\\'; }
    # Wine keeps long names: the sources include "interrupt.h", which DOS truncates to INTERRUP.H
    inc=$PWD/.work/ass/inc
    mkdir -p "$inc"
    ln -sf "$PWD/src/ass/INTERRUP.H" "$inc/interrupt.h"
    while read -r m f; do
        (cd src/ass && WINEPREFIX=$PWD/../../.work/toolchains/wineprefix WINEDEBUG=-all \
            WATCOM="$(winpath "$r")" INCLUDE="$(winpath "$r/H")" PATH="$r/BINB:$r/BINNT:$PATH" \
            timeout 120 wine "$r/BINNT/wcc386.exe" $f /i=. "/i=$(winpath "$inc")" -DLIBVER_ASSREV=${rev}L $m.C \
            "/fo=$(winpath "$PWD/../../$out/$m.OBJ")" >> "../../$out/WCC.OUT" 2>&1 < /dev/null) || true
    done < "$wine"
fi
tr -d '\r' < "$out/WCC.OUT" | grep -E 'Error!' | head -8 || true
want=$(echo $mods | wc -w); got=0
for m in $mods; do [ -f "$out/$m.OBJ" ] && got=$((got + 1)); done
echo "$out: $got of $want modules built"
[ "$got" = "$want" ]

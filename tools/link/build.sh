#!/bin/sh
# Hybrid link: our C module (src/ww.c + tools/link/extra.c) with everything else of WW.EXE delinked
# (tools/link/delink.py), linked by WLINK 9.5b under the stock DOSBox into .work/link/WWNEW.EXE.
#
#   tools/link/build.sh            build; prints WLINK's messages and the map's summary
#   ORIG=fn_173c4,track_load tools/link/build.sh   the same, with those C functions taken from
#                                  the game's bytes (stripfn.py + delink.py DELINK_ORIG)
#   DELINK_ASM=0 tools/link/build.sh   the assembler module from the game's bytes, not src/wwasm.asm
#
# Steps: compile -> symmap.py (address of every external) -> cpublics.py -> delink.py -> WW.LNK -> WLINK.
set -eu
cd "$(dirname "$0")/../.."
wc=.work/toolchains/inst/WC95B
w=.work/link
mkdir -p $w
if [ -n "${ORIG:-}" ]; then        # ORIG=fn,fn: those functions from the game's bytes, not ours
    python3 tools/link/stripfn.py src/ww.c $w/WW.C "$ORIG"
else
    cp src/ww.c $w/WW.C
fi
cp tools/link/extra.c $w/EXTRA.C
rm -f $w/WW.OBJ $w/EXTRA.OBJ $w/WWNEW.EXE
tools/wc/wcc.sh $wc $w WW.C -s -3r -oneatx | grep -iE ' error|errors' || true
tools/wc/wcc.sh $wc $w EXTRA.C -s -3r -oneatx | grep -iE ' error|errors' || true
[ -f $w/WW.OBJ ] && [ -f $w/EXTRA.OBJ ] || { echo "compile failed"; exit 1; }
if [ "${DELINK_ASM:-1}" != 0 ]; then      # the assembler module from src/wwasm.asm (tools/asm/asmcheck.py)
    segof=$(sed -n 's/^ *mov bx, seg \([A-Za-z_0-9]*\).*/\1/p' src/wwasm.asm)   # kbd_isr's DS (omfsegfix.py)
    nasm -f obj -o $w/WWASM.OBJ src/wwasm.asm && python3 tools/asm/omfsegfix.py $w/WWASM.OBJ "$segof" >/dev/null
    asmfile="file WWASM.OBJ"
else
    rm -f $w/WWASM.OBJ; asmfile=""
fi
python3 tools/link/symmap.py $w/WW.OBJ --out $w/symmap.txt
python3 tools/link/cpublics.py $w/WW.OBJ $w/EXTRA.OBJ > $w/cpublics.txt
DELINK_ORIG="${ORIG:-}" python3 tools/link/delink.py original/WW.EXE $w/symmap.txt $w/BLOB.OBJ
# BLOB.OBJ first: its data segment then opens DGROUP at 0x60000 like WW.EXE's data object, so the
# literal addresses src/ww.c uses ((char *)0x80ace, 28 lines) and every names-file address hold
# names our source declares under an old name next to the defining one (one function, two names)
cat > $w/WW.LNK <<LNK
system dos4g
name WWNEW.EXE
option map=WWNEW.MAP
alias fn_32e60_=songs_pick_
alias fn_279e8_=track_load_
alias fn_2754c_=effects_load_
alias fn_27920_=wait_input_
alias f29e0c_=hud_draw_
file BLOB.OBJ
file WW.OBJ
file EXTRA.OBJ
$asmfile
LNK
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 300 dosbox -noconsole -exit \
    -c "mount c \"$(cd $wc && pwd)\"" -c "mount d \"$(cd $w && pwd)\"" -c "set WATCOM=C:\\" \
    -c "set PATH=C:\\BIN;C:\\BINB" -c "set DOS4G=QUIET" -c "set LIB=C:\\LIB386;C:\\LIB386\\DOS" -c "d:" \
    -c "C:\\BIN\\WLINK.EXE @WW.LNK > LINK.OUT" -c exit >/dev/null 2>&1 || true
tr -d '\r' < $w/LINK.OUT | grep -v '^$' | head -40
[ -f $w/WWNEW.EXE ] && ls -la $w/WWNEW.EXE

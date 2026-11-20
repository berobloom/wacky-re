#!/bin/sh
# Compile C with a Watcom C/C++32 install, headless under the stock DOSBox.
#
#   tools/wc/wcc.sh WCDIR SRCDIR FILE.C [wcc386 options]
#
# WCDIR is an install (e.g. .work/toolchains/inst/WC95B: BIN, H, LIB386); SRCDIR holds FILE.C and receives
# FILE.OBJ and WCC.OUT (the compiler's messages). Exit 0 when FILE.OBJ is newer than the start of this run.
# DOSBox runs on the dummy video driver, so nothing opens on screen. WCC=WPP386 picks another compiler in BIN.
set -eu
[ $# -ge 3 ] || { sed -n 2,9p "$0"; exit 2; }
wc=$(cd "$1" && pwd); src=$(cd "$2" && pwd); file=$3; shift 3
base=${file%.*}
exe=${WCC:-WCC386}; bin=BIN
[ -f "$wc/BIN/$exe.EXE" ] || bin=BINB          # 10.0a keeps the DOS-hosted compilers in BINB only
stamp=$(mktemp); trap 'rm -f "$stamp"' EXIT
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 300 dosbox -noconsole -exit \
    -c "mount c \"$wc\"" -c "mount d \"$src\"" -c "set WATCOM=C:\\" -c "set INCLUDE=C:\\H" \
    -c "set PATH=C:\\BIN;C:\\BINB" -c "set DOS4G=QUIET" -c "d:" \
    -c "C:\\$bin\\$exe.EXE $* $file > WCC.OUT" -c exit >/dev/null 2>&1 || true
tr -d '\r' < "$src/WCC.OUT" | grep -v '^$' | tail -8
[ "$src/$base.OBJ" -nt "$stamp" ] || [ "$src/$(echo "$base" | tr a-z A-Z).OBJ" -nt "$stamp" ]

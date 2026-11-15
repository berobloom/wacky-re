#!/bin/sh
# Compile a Turbo Pascal program headless with the stock DOSBox.
#
#   tools/tp/tpc.sh TPDIR SRCDIR FILE.PAS [TPC options]
#
# TPDIR holds TPC.EXE and TURBO.TPL (e.g. .work/toolchains/tp60); SRCDIR is the
# folder with FILE.PAS, where FILE.EXE and TPC.OUT (the compiler's messages)
# are written. Exit 0 when FILE.EXE is newer than the start of this run.
# DOSBox is on a dummy video driver, so nothing opens on screen.
set -eu
[ $# -ge 3 ] || { sed -n 2,10p "$0"; exit 2; }
tp=$(cd "$1" && pwd); src=$(cd "$2" && pwd); file=$3; shift 3
base=${file%.*}
stamp=$(mktemp); trap 'rm -f "$stamp"' EXIT
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 120 dosbox -noconsole -exit \
    -c "mount c \"$tp\"" -c "mount d \"$src\"" -c "d:" \
    -c "c:\\TPC.EXE $* $file > TPC.OUT" -c exit >/dev/null 2>&1 || true
tr -d '\r' < "$src/TPC.OUT" | grep -v '^$' | tail -5
[ "$src/$base.EXE" -nt "$stamp" ] || [ "$src/$(echo "$base" | tr a-z A-Z).EXE" -nt "$stamp" ]

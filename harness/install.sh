#!/bin/sh
# Lay the game out for DOSBox-X and write .work/dos/game.conf.
#
#   harness/install.sh [source dir]    (default: original/)
#
# Default: copy the files of original/ into .work/dos/C/<GAMEDIR> (the copy is
# what the game may write to; original/ stays untouched). If the game has its
# own installer batch file, replay its COPY lines instead (see the redcat
# project's reverse-engineering/dosbox/install.sh for INSTMAX.BAT), so the
# layout is the one the installer makes.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
SRC=$(cd "${1:-$ROOT/original}" && pwd)
GAMEDIR=${GAMEDIR:-GAME}
DOS="$ROOT/.work/dos"
GAME="$DOS/C/$GAMEDIR"
mkdir -p "$GAME" "$DOS/capture" "$DOS/states"
for f in "$SRC"/*; do
    [ -f "$f" ] || continue
    case "$(basename "$f")" in SHA256SUMS|README.md) continue ;; esac
    cp "$f" "$GAME/$(basename "$f" | tr 'a-z' 'A-Z')"
done
sed -e "s|@C@|$DOS/C|" -e "s|@CD@|$SRC|" "$HERE/game.conf.in" > "$DOS/game.conf"
echo "installed to $GAME ($(ls "$GAME" | wc -l) files); config $DOS/game.conf"

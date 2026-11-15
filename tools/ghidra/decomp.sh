#!/bin/sh
# Decompile a whole program with Ghidra headless into .work/ghidra/<prog>.c
# (names from analysis/<prog>_names.txt applied; one "// ==== addr name"
# header per function, so grep and sed can find them).
#   tools/ghidra/decomp.sh <prog>                    LE: .work/le/<prog>.elf
#   tools/ghidra/decomp.sh <prog> FILE.EXE 0x10000   MZ: names are rel 0000:XXXX,
#                                                    Ghidra loads at 1000:0000
# Set TMPDIR to a directory without a dot-directory in its path.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
PROG=${1:?usage: decomp.sh <prog> [input] [base]}
IN=${2:-$ROOT/.work/le/$PROG.elf}
BASE=${3:-0}
GHIDRA=${GHIDRA_INSTALL_DIR:-$HOME/Downloads/ghidra_12.1.4_PUBLIC}
OUT="$ROOT/.work/ghidra"
mkdir -p "$OUT"
# Ghidra refuses project paths with a dot-directory in them (.work)
PROJ=$(mktemp -d "${TMPDIR:-/tmp}/drea-ghidra.XXXXXX")
trap 'rm -rf "$PROJ"' EXIT
A="$ROOT/analysis"
"$GHIDRA/support/analyzeHeadless" "$PROJ" "$PROG" -overwrite \
    -import "$IN" \
    -scriptPath "$HERE" \
    -postScript DumpDecomp.java "$A/${PROG}_names.txt" "$A/${PROG}_funcs.tsv" "$OUT/$PROG.c" "$BASE" \
    -deleteProject > "$OUT/$PROG.log" 2>&1
grep -c '^// ====' "$OUT/$PROG.c" | sed "s|^|$OUT/$PROG.c: |; s|$| functions|"

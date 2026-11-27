#!/bin/sh
# permute_all.sh: run tools/wc/permute.py over a list of near misses, JOBS at a time.
#   [PERMUTER=astperm] [PERMUTE_FLAGS='--weighted ...'] tools/wc/permute_all.sh LIST [JOBS [ROUNDS [COPIES]]]
# PERMUTER picks tools/wc/permute.py (default) or astperm.py; PERMUTE_FLAGS go to every run (see its usage). Runs are niced: the compiles keep a
# laptop's fans busy for hours.
# LIST lines: "name ourbytes 0xADDR" (src/check.sh's "differs from" lines). The game range scored is
# [ADDR, ADDR + ourbytes), as fndiff does. Logs go to .work/perm_all/<name>.log; summarise with
# `grep -H 'final best\|^control' .work/perm_all/*.log` (logs are NAME.permute.log / NAME.astperm.log).
cd "$(dirname "$0")/../.." || exit 1
list=$1; jobs=${2:-6}; rounds=${3:-15}; copies=${4:-30}
p=${PERMUTER:-permute}; export p
mkdir -p .work/perm_all
seed=100
while read -r n sz a; do
    seed=$((seed + 1))
    end=$(printf '0x%x' $((a + sz)))
    echo "$n $a $end $seed"
done < "$list" | xargs -P "$jobs" -L 1 sh -c \
    'nice -n 19 .work/venv/bin/python tools/wc/$p.py $PERMUTE_FLAGS "$0" "$1" "$2" '"$rounds $copies"' "$3" > .work/perm_all/$0.$p.log 2>&1'

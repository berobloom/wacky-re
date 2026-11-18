#!/bin/sh
# permute_all.sh: run tools/wc/permute.py over a list of near misses, JOBS at a time.
#   tools/wc/permute_all.sh LIST [JOBS [ROUNDS [COPIES]]]
# LIST lines: "name ourbytes 0xADDR" (src/check.sh's "differs from" lines). The game range scored is
# [ADDR, ADDR + ourbytes), as fndiff does. Logs go to .work/perm_all/<name>.log; summarise with
# `grep -H 'final best\|^control' .work/perm_all/*.log`.
cd "$(dirname "$0")/../.." || exit 1
list=$1; jobs=${2:-6}; rounds=${3:-15}; copies=${4:-30}
mkdir -p .work/perm_all
seed=100
while read -r n sz a; do
    seed=$((seed + 1))
    end=$(printf '0x%x' $((a + sz)))
    echo "$n $a $end $seed"
done < "$list" | xargs -P "$jobs" -L 1 sh -c \
    '.work/venv/bin/python tools/wc/permute.py "$0" "$1" "$2" '"$rounds $copies"' "$3" > .work/perm_all/$0.log 2>&1'

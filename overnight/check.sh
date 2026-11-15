#!/usr/bin/env bash
# Health gate for overnight passes. Checks a committed revision in a throwaway worktree (with
# untracked original/ and .work/ symlinked in), so the result reflects what is in git.
# Usage: overnight/check.sh [rev]. Exit 0 = healthy; output on failure is fed back to the agent.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REV=${1:-HEAD}
WT=$(mktemp -d "${TMPDIR:-/tmp}/wackycheck.XXXXXX")
trap 'git -C "$ROOT" worktree remove --force "$WT" >/dev/null 2>&1; rm -rf "$WT"' EXIT
git -C "$ROOT" worktree add --detach -q "$WT" "$REV" || { echo "FAIL: cannot check out $REV"; exit 1; }
ln -s "$ROOT/original" "$WT/original"
ln -s "$ROOT/.work" "$WT/.work"
cd "$WT"
fail=0
bad() { echo "FAIL: $*"; fail=1; }

# 1. shipped files are untouched (original/ is untracked; verify the real directory)
(cd "$ROOT/original" && sha256sum --quiet -c SHA256SUMS) || bad "original/ was modified (restore from the game archive; never edit it)"

# 2. every tool compiles
for f in $(find tools -name '*.py'); do python3 -m py_compile "$f" 2>&1 || bad "$f does not compile"; done

# 3. the matching rebuild: src/ww.c must still compile with the game's options and keep its
#    byte-identical functions. Raise src/matched.baseline in the same commit when you match more.
rm -f .work/src/WW.OBJ
out=$(src/check.sh 2>&1); rc=$?
[ $rc -eq 0 ] || { bad "src/check.sh fails:"; echo "$out" | grep -iE 'error|Error' | head -15; }
m=$(echo "$out" | grep -c ' MATCH ')
b=$(cat src/matched.baseline 2>/dev/null || echo 0)
[ "$m" -ge "$b" ] || bad "src/ww.c now matches only $m functions, baseline is $b (a regression): re-introduce what broke"

# 4. STATUS exists, stays bounded, and has numbered Next items
[ -f STATUS.md ] || bad "STATUS.md missing"
n=$(wc -l < STATUS.md 2>/dev/null || echo 999)
[ "$n" -le 60 ] || bad "STATUS.md has $n lines (limit 60: history belongs in git, not STATUS)"
grep -qE '^[0-9]+\. ' STATUS.md || bad "STATUS.md has no numbered 'Next' items"
[ -f findings/INDEX.md ] || bad "findings/INDEX.md missing"

# 5. no big binaries committed (captures/original stay out of git)
big=$(git ls-files -z | xargs -0 -r du -k 2>/dev/null | awk '$1>4096{print $2}' | grep -v '\.rea-snapshot$')
[ -z "$big" ] || bad "files over 4 MB committed: $big"

# 6. once the port has a build, it must build
if [ -f port/Makefile ]; then
  out=$(make -C port -s 2>&1) || { bad "port build fails:"; echo "$out" | tail -20; }
  out=$(make -C port -s test 2>&1) || { bad "make -C port test fails:"; echo "$out" | grep -E 'FAIL|Error' | head -20; }
fi
if [ -f port/CMakeLists.txt ]; then
  out=$( { cmake -S port -B port/_build && cmake --build port/_build -j; } 2>&1) || { bad "port cmake build fails:"; echo "$out" | tail -20; }
fi

# 7. tool test suites that exist must pass
for f in tools/test_*.py tools/*/test_*.py; do
  [ -e "$f" ] || continue
  out=$(python3 "$f" 2>&1) || { bad "$f fails:"; echo "$out" | tail -8; }
done

# 8. hedge words in the port may not grow above the baseline (file absent = skipped)
if [ -f tools/hedge_count.py ]; then
  out=$(python3 tools/hedge_count.py --max "$(cat "$ROOT/overnight/hedges.baseline" 2>/dev/null || echo 999999)" 2>&1) || bad "$out"
fi

[ $fail -eq 0 ] && echo "OK: $REV healthy ($m matched functions)"
exit $fail

#!/usr/bin/env bash
# Kill only THIS project's harness processes. Other projects (re_cold, SAM) run their own
# DOSBox-X/gdb/Xvfb on this machine: never pkill by bare name. Patterns use [x] so pgrep
# never matches this script's own command line.
for pat in 'wacky-re/[.]work/dos/game[.]conf' '[d]osrun\.py' '[X]vfb :57'; do
  for pid in $(pgrep -f "$pat"); do
    [ "$pid" = "$$" ] && continue
    kill "$pid" 2>/dev/null && echo "cleanup: killed $pid ($pat)"
  done
done
sleep 1
for pat in 'wacky-re/[.]work/dos/game[.]conf' '[X]vfb :57'; do
  for pid in $(pgrep -f "$pat"); do kill -9 "$pid" 2>/dev/null; done
done
exit 0

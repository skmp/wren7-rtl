#!/bin/bash
# run_hw.sh SUITE... -- run tests/hw/SUITE/jobs.txt on the console: builds the runner and the ARM programs, copies the
# job file to build/hw/jobs.txt, runs build/hw/runner.elf through shrike4's hwrun.sh (the only way to use the
# console; it serializes all users).  Results: tests/hw/SUITE/hw/ (results.txt, <job>.bin, console.log, status).
M=$(cd "$(dirname "$0")" && pwd)
HWRUN=/home/skmp/projects/dreamster/shrike4-rtl/tools/hw/hwrun.sh
source /opt/toolchains/dc/kos/environ.sh
set -u
make -C "$M/hw" >/dev/null || { echo "build failed"; exit 1; }
rc_all=0
for s in "$@"; do
  d="$M/tests/hw/$s/hw"; mkdir -p "$d"
  cp "$M/tests/hw/$s/jobs.txt" "$M/../build/hw/jobs.txt" || { rc_all=1; continue; }
  echo "== hw $s $(date +%T)"
  "$HWRUN" "$M/../build/hw/runner.elf" "$d/console.log.raw" > "$d/console.log" 2>&1
  rc=$?; echo "$rc" > "$d/status"; rm -f "$d/console.log.raw"
  echo "== hw $s exit $rc $(date +%T)"
  [ $rc -ne 0 ] && rc_all=$rc
done
exit $rc_all

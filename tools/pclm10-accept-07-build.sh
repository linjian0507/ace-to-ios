#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-07-2026-11-05
mkdir -p "$out"
{
  echo "acceptance-07 hypothesis: the 6 MiB image runs sleepd/chronod, wifid and the widget extensions at boot;"
  echo "a lean image (SLEEPD=0, no GL, no REST) should reach the home screen in tens of seconds, not ~100 s."
  echo "start: $(date -Is)"
} > "$out/start.txt"
cp -a userland/root.apfs "$out/root.apfs.before-07"
sha256sum userland/root.apfs >> "$out/start.txt"
cp -a userland/rc.jobs "$out/rc.jobs.used" 2>/dev/null || true
cd userland
SLEEPD=0 ./build.sh > "$out/build.log" 2>&1
rc=$?
echo "build rc=$rc" >> "$out/start.txt"
ls -la root.apfs >> "$out/start.txt"
sha256sum root.apfs >> "$out/start.txt"
echo "end: $(date -Is)" >> "$out/start.txt"

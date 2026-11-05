#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-12-2026-11-05
mkdir -p "$out"
cp -a userland/root.apfs "$out/root.apfs.default-lean"   # 215a23...
{
  echo "acceptance-12: default small image + rc.jobs.autotest (software path, SLEEPD on, no GL)."
  echo "The job injects unlock/tap/Home/swipes/volume keys; the host screenshots on a timer."
  echo "start: $(date -Is)"
} > "$out/start.txt"
cp /mnt/s/opencode/hakios/userland/rc.jobs.autotest userland/rc.jobs.autotest
cd userland
RC_JOBS=rc.jobs.autotest ./build.sh > "$out/build.log" 2>&1
rc=$?
echo "build rc=$rc" >> "$out/start.txt"
ls -la root.apfs >> "$out/start.txt"
sha256sum root.apfs >> "$out/start.txt"
echo "end: $(date -Is)" >> "$out/start.txt"

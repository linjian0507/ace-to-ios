#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-13-2026-11-05
mkdir -p "$out"
cp -a userland/root.apfs "$out/root.apfs.autotest-12" 2>/dev/null || true
{
  echo "acceptance-13: rc.jobs.verify (net_test loop + cpu_test after 30 s), cpufreq=0, 180 s."
  echo "start: $(date -Is)"
} > "$out/start.txt"
cp /mnt/s/opencode/hakios/userland/rc.jobs.verify userland/rc.jobs.verify
cd userland
RC_JOBS=rc.jobs.verify ./build.sh > "$out/build.log" 2>&1
echo "build rc=$?" >> "$out/start.txt"
ls -la root.apfs >> "$out/start.txt"
sha256sum root.apfs >> "$out/start.txt"

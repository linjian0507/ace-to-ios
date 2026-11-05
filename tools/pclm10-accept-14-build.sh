#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-14-2026-11-05
mkdir -p "$out"
cp /mnt/s/opencode/hakios/userland/rc.jobs.cpumark userland/rc.jobs.cpumark
cd userland
RC_JOBS=rc.jobs.cpumark ./build.sh > "$out/build.log" 2>&1
echo "build rc=$?" > "$out/start.txt"
sha256sum root.apfs >> "$out/start.txt"
ls -la root.apfs >> "$out/start.txt"

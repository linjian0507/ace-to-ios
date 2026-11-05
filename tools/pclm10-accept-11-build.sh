#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-11-2026-11-05
mkdir -p "$out"
cp -a userland/root.apfs "$out/root.apfs.lean-gl"   # keep the lean GL image (bdf9d2...)
{
  echo "acceptance-11: default small image (SLEEPD on, no GL), software path, cpufreq=0."
  echo "start: $(date -Is)"
} > "$out/start.txt"
cd userland
./build.sh > "$out/build.log" 2>&1
rc=$?
echo "build rc=$rc" >> "$out/start.txt"
ls -la root.apfs >> "$out/start.txt"
sha256sum root.apfs >> "$out/start.txt"
echo "end: $(date -Is)" >> "$out/start.txt"

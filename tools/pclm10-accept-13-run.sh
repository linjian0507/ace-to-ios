#!/bin/bash
set -uo pipefail
cd /root/hakios
out=/mnt/s/opencode/hakios/logs/acceptance-13-2026-11-05
{
  echo "run start: $(date -Is)"
  sha256sum userland/root.apfs hakios-kernel
} >> "$out/start.txt"
(
  for t in 30 60 90 120; do
    sleep 30
    ./xl screenshot "$out/shot-$t.png" >>"$out/screenshots.log" 2>&1 || true
  done
) &
shots=$!
export XL_SECS=180 XL_RAMDISK=userland/root.apfs XL_HOME=fastboot XL_FOLLOW=10 XL_FOLLOW_KEEP=1
tools/pclm10-run.sh bootcpu=7 cpufreq=0 touch=1 pon=1 thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60 >"$out/wrapper.log" 2>&1
echo "out of the run: rc=$? $(date -Is)" >> "$out/start.txt"
wait $shots || true
log=$(ls -t logs/*-run.log 2>/dev/null | head -1)
[ -n "$log" ] && cp -f "$log" "$out/"
echo "end: $(date -Is)" >> "$out/start.txt"

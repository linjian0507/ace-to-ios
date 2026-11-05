#!/bin/bash
set -uo pipefail
cd /root/hakios
out=/mnt/s/opencode/hakios/logs/acceptance-12-2026-11-05
mkdir -p "$out"
{
  echo "run start: $(date -Is)"
  sha256sum userland/root.apfs hakios-kernel
} | tee -a "$out/start.txt"
(
  prev=0
  for t in 30 60 75 90 110 150 200 250; do
    sleep $((t - prev)); prev=$t
    ./xl screenshot "$out/shot-$t.png" >>"$out/screenshots.log" 2>&1 || echo "shot-$t failed" >>"$out/screenshots.log"
  done
) &
shots=$!
export XL_SECS=300 XL_RAMDISK=userland/root.apfs XL_HOME=fastboot XL_FOLLOW=10 XL_FOLLOW_KEEP=1
tools/pclm10-run.sh bootcpu=7 cpufreq=0 touch=1 pon=1 thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60 >"$out/wrapper.log" 2>&1
echo "out of the run: rc=$? $(date -Is)" | tee -a "$out/start.txt"
wait $shots || true
log=$(ls -t logs/*-run.log 2>/dev/null | head -1)
[ -n "$log" ] && cp -f "$log" "$out/"
./xl screenshot "$out/shot-final.png" >>"$out/screenshots.log" 2>&1 || true
echo "end: $(date -Is)" | tee -a "$out/start.txt"

#!/bin/bash
set -uo pipefail
cd /root/hakios
out=/mnt/s/opencode/hakios/logs/acceptance-09-2026-11-05
mkdir -p "$out"
{
  echo "acceptance-09: GPU retry after cold reset + resident re-upload; lean GL image."
  echo "run start: $(date -Is)"
  sha256sum userland/root.apfs hakios-kernel xnuloader/loader.efi
  ls -l userland/root.apfs
} | tee -a "$out/start.txt"

(
  for t in 30 60 120 200 260; do
    sleep 30
    ./xl screenshot "$out/shot-$t.png" >>"$out/screenshots.log" 2>&1 || echo "shot-$t failed" >>"$out/screenshots.log"
  done
) &
shots=$!

read -r -a gpu_args < devices/pclm10/configs/interactive-gpu.args
export XL_SECS=300 XL_RAMDISK=userland/root.apfs XL_HOME=fastboot XL_FOLLOW=10 XL_FOLLOW_KEEP=1
tools/pclm10-run.sh bootcpu=7 cpufreq=0 touch=1 pon=1 thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60 "${gpu_args[@]}" >"$out/wrapper.log" 2>&1
rc=$?
echo "out of the run: rc=$rc $(date -Is)" | tee -a "$out/start.txt"
wait $shots || true
sleep 2
./xl screenshot "$out/shot-final.png" >>"$out/screenshots.log" 2>&1 || true
log=$(ls -t logs/*-run.log 2>/dev/null | head -1)
if [ -n "$log" ]; then cp -f "$log" "$out/"; echo "run log: $log" | tee -a "$out/start.txt"; fi
echo "end: $(date -Is)" | tee -a "$out/start.txt"

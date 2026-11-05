#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-15-2026-11-05
mkdir -p "$out"
cp -f /mnt/s/opencode/hakios/logs/delivery-2026-11-05/root.apfs userland/root.apfs
{
  echo "acceptance-15: merged kernel (upstream 87bf485 + pclm10) on the verified 4 MiB image."
  echo "run start: $(date -Is)"
  sha256sum hakios-kernel userland/root.apfs xnuloader/loader.efi
  ls -l hakios-kernel userland/root.apfs
} | tee -a "$out/start.txt"
(
  prev=0
  for t in 40 80 120 160; do
    sleep $((t - prev)); prev=$t
    ./xl screenshot "$out/shot-$t.png" >>"$out/screenshots.log" 2>&1 || true
  done
) &
shots=$!
export XL_SECS=200 XL_RAMDISK=userland/root.apfs XL_HOME=fastboot XL_FOLLOW=10 XL_FOLLOW_KEEP=1
tools/pclm10-run.sh bootcpu=7 cpufreq=0 touch=1 pon=1 thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60 >"$out/wrapper.log" 2>&1
echo "out of the run: rc=$? $(date -Is)" | tee -a "$out/start.txt"
wait $shots || true
log=$(ls -t logs/*-run.log 2>/dev/null | head -1)
[ -n "$log" ] && cp -f "$log" "$out/"
./xl screenshot "$out/shot-final.png" >>"$out/screenshots.log" 2>&1 || true
echo "end: $(date -Is)" | tee -a "$out/start.txt"

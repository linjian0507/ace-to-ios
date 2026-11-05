#!/bin/bash
set -uo pipefail
cd /root/hakios
out=/mnt/s/opencode/hakios/logs/acceptance-07-2026-11-05
mkdir -p "$out"

# cooling: at least ten minutes after the 13:27:57 return of acceptance 06
target=$(date -d '2026-11-05 13:39:00' +%s)
now=$(date +%s)
if [ "$now" -lt "$target" ]; then
  echo "cooling: sleeping $((target - now)) s" | tee -a "$out/start.txt"
  sleep $((target - now))
fi

{
  echo "run start: $(date -Is)"
  sha256sum userland/root.apfs hakios-kernel xnuloader/loader.efi
  ls -l userland/root.apfs
} | tee -a "$out/start.txt"

# screenshots through the run; the kernel returns on its own at 300 s
(
  for t in 20 60 120 200 260; do
    sleep 20
    ./xl screenshot "$out/shot-$t.png" >>"$out/screenshots.log" 2>&1 || echo "shot-$t failed" >>"$out/screenshots.log"
  done
) &
shots=$!

export XL_SECS=300 XL_RAMDISK=userland/root.apfs XL_HOME=fastboot XL_FOLLOW=10 XL_FOLLOW_KEEP=1
tools/pclm10-run.sh bootcpu=7 cpufreq=1 touch=1 pon=1 thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60 >"$out/wrapper.log" 2>&1
rc=$?
echo "out of the run: rc=$rc $(date -Is)" | tee -a "$out/start.txt"
wait $shots || true
sleep 2
./xl screenshot "$out/shot-final.png" >>"$out/screenshots.log" 2>&1 || true
log=$(ls -t logs/*-run.log 2>/dev/null | head -1)
if [ -n "$log" ]; then cp -f "$log" "$out/"; echo "run log: $log" | tee -a "$out/start.txt"; fi
echo "end: $(date -Is)" | tee -a "$out/start.txt"

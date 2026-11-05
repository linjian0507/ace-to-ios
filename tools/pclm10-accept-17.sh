#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-17-2026-11-05
mkdir -p "$out"
target=$(date -d '2026-11-05 15:27:00' +%s); now=$(date +%s)
[ "$now" -lt "$target" ] && { echo "cooling $((target-now)) s" >> "$out/start.txt"; sleep $((target-now)); }
{
  echo "acceptance-17: merged kernel, controlled flow, no mid-run screenshots, log fetched first."
  echo "run start: $(date -Is)"
  sha256sum hakios-kernel userland/root.apfs
} >> "$out/start.txt"
./xl reboot 2>&1 | tail -1
for i in $(seq 1 40); do sleep 3; s=$(./xl state 2>/dev/null); [ "$s" = loader ] && break; done
./xl ramdisk userland/system.apfs 2>&1 | tail -1 | tee -a "$out/start.txt"
args="cpus=8 bq27541=1 wled=1 smb5=1 cmdb=0x85f20000 epss0=0x18323000 epss1=0x18325800 epss2=0x18327800 epss_hw=1 bootcpu=7 cpufreq=0 touch=1 pon=1 thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60"
xnuloader/mkpayload.py -o /tmp/p6.bin kernel=hakios-kernel ramdisk=userland/root.apfs --cmdline "loader_return_after=150 epoch=$(date +%s) sb_delay=8 $args" | tail -1
timeout 30 fastboot stage /tmp/p6.bin 2>&1 | tail -1
timeout 20 fastboot oem run 2>&1 | tail -1
timeout 15 fastboot getvar result 2>&1 | grep '^result:' | tee -a "$out/start.txt"
for i in $(seq 1 80); do
  sleep 3
  r=$(timeout 10 fastboot getvar result 2>/dev/null | grep '^result:' | tail -1)
  case "$r" in *running*) ;; *) break ;; esac
done
echo "final: $r" >> "$out/start.txt"
# THE LOG FIRST: no oem command may run before this
timeout 90 fastboot get_staged /tmp/r6.log 2>&1 | tail -1
cp -f /tmp/r6.log "$out/run.log"
ls -l "$out/run.log" >> "$out/start.txt"
grep -a -n -E "resident image|BSD root|exec of /sbin/launchd|panic\(|thermal_stop|at or above|SpringBoard created|no GPU draws|kernel returned" "$out/run.log" | head -12 >> "$out/start.txt"
# now a picture of whatever stayed on the panel
./xl screenshot "$out/shot-final.png" >> "$out/screenshots.log" 2>&1 || true

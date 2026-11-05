#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-18-2026-11-05
mkdir -p "$out"
{
  echo "acceptance-18: merged kernel, correct poll (empty answer retried), log before any oem command."
  echo "run start: $(date -Is)"
  sha256sum hakios-kernel userland/root.apfs
} >> "$out/start.txt"
./xl reboot 2>&1 | tail -1
for i in $(seq 1 40); do sleep 3; s=$(./xl state 2>/dev/null); [ "$s" = loader ] && break; done
./xl ramdisk userland/system.apfs 2>&1 | tail -1 | tee -a "$out/start.txt"
args="cpus=8 bq27541=1 wled=1 smb5=1 cmdb=0x85f20000 epss0=0x18323000 epss1=0x18325800 epss2=0x18327800 epss_hw=1 bootcpu=7 cpufreq=0 touch=1 pon=1 thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60"
xnuloader/mkpayload.py -o /tmp/p7.bin kernel=hakios-kernel ramdisk=userland/root.apfs --cmdline "loader_return_after=150 epoch=$(date +%s) sb_delay=8 $args" | tail -1
timeout 30 fastboot stage /tmp/p7.bin 2>&1 | tail -1
timeout 20 fastboot oem run 2>&1 | tail -1
start=$(date +%s)
while :; do
  sleep 3
  now=$(date +%s); [ $((now - start)) -gt 240 ] && { echo "poll timeout" >> "$out/start.txt"; break; }
  r=$(timeout 10 fastboot getvar result 2>/dev/null | grep '^result:' | tail -1)
  [ -z "$r" ] && continue
  case "$r" in *running*) echo "still running $((now-start))s" >> "$out/start.txt" ;; *) echo "final: $r" >> "$out/start.txt"; break ;; esac
done
# log first, no oem command before this
timeout 90 fastboot get_staged /tmp/r7.log 2>&1 | tail -1
cp -f /tmp/r7.log "$out/run.log"
ls -l "$out/run.log" >> "$out/start.txt"
grep -a -n -E "resident image|BSD root|exec of /sbin/launchd|panic\(|thermal_stop|at or above|SpringBoard|no GPU draws|kernel returned" "$out/run.log" | head -14 >> "$out/start.txt"
./xl screenshot "$out/shot-final.png" >> "$out/screenshots.log" 2>&1 || true

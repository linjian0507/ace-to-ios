#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-15-2026-11-05
echo "=== fresh loader session $(date -Is) ===" | tee -a "$out/fresh-session.log"
./xl reboot 2>&1 | tail -2
for i in $(seq 1 40); do
  sleep 3
  s=$(./xl state 2>/dev/null)
  echo "state: $s" >> "$out/fresh-session.log"
  [ "$s" = loader ] && break
done
echo "state now: $(./xl state)" | tee -a "$out/fresh-session.log"
echo "=== upload resident root $(date -Is) ===" | tee -a "$out/fresh-session.log"
./xl ramdisk userland/system.apfs 2>&1 | tail -2
echo "=== run 200 s $(date -Is) ===" | tee -a "$out/fresh-session.log"
exec bash tools/pclm10-accept-15-merged.sh

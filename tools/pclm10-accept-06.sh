#!/bin/bash
# Acceptance 06: candidate 05 (A640 identity, quiet GPU submits) — bounded RAM run.
# No flash, no persistent write; screenshots during the run; artifacts archived.
set -uo pipefail
cd /root/hakios
out=/mnt/s/opencode/hakios/logs/acceptance-06-2026-11-05
mkdir -p "$out"
{
  echo "start: $(date -Is)"
  echo "kernel: $(sha256sum hakios-kernel)"
  echo "loader: $(sha256sum xnuloader/loader.efi)"
  echo "root:   $(sha256sum userland/root.apfs)"
  echo "system: $(sha256sum userland/system.apfs)"
  echo "host uptime: $(uptime -p)"
  echo "hold present: $(test -f devices/pclm10/configs/interactive-hold.txt && echo yes || echo no)"
} | tee "$out/start.txt"

(
  sleep 30
  ./xl screenshot "$out/shot-30.png" >>"$out/screenshots.log" 2>&1 || echo "shot-30 failed" >>"$out/screenshots.log"
  sleep 45
  ./xl screenshot "$out/shot-75.png" >>"$out/screenshots.log" 2>&1 || echo "shot-75 failed" >>"$out/screenshots.log"
  sleep 45
  ./xl screenshot "$out/shot-120.png" >>"$out/screenshots.log" 2>&1 || echo "shot-120 failed" >>"$out/screenshots.log"
) &
shots=$!

tools/pclm10-interactive.sh >"$out/wrapper.log" 2>&1
rc=$?
echo "interactive exit: $rc" | tee -a "$out/start.txt"
wait $shots || true
sleep 2
./xl screenshot "$out/shot-final.png" >>"$out/screenshots.log" 2>&1 || true
log=$(ls -t logs/*-run.log 2>/dev/null | head -1)
if [ -n "$log" ]; then
  cp -f "$log" "$out/"
  echo "run log: $log -> $out/$(basename "$log")" | tee -a "$out/start.txt"
else
  echo "no run log found" | tee -a "$out/start.txt"
fi
echo "end: $(date -Is)" | tee -a "$out/start.txt"

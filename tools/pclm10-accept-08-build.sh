#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
echo "--- GL libs present? ---"
ls -la userland/lib/libOSMesa.8.dylib userland/lib/libhakios.gl.dylib 2>&1
ls -la userland/lib/HakiosMetal.bundle 2>&1 | head -3
echo "--- build lean+GL ---"
out=/mnt/s/opencode/hakios/logs/acceptance-08-2026-11-05
mkdir -p "$out"
{
  echo "acceptance-08: lean image + GL=1 (GPU path), SLEEPD=0, no REST."
  echo "start: $(date -Is)"
} > "$out/start.txt"
cp -a userland/root.apfs "$out/root.apfs.4mib-software"   # keep the fast lean software image
cd userland
GL=1 SLEEPD=0 ./build.sh > "$out/build.log" 2>&1
rc=$?
echo "build rc=$rc" >> "$out/start.txt"
ls -la root.apfs >> "$out/start.txt"
sha256sum root.apfs >> "$out/start.txt"
python3 ../tools/apfs/apfs.py ls root.apfs /lib 2>/dev/null | grep -E "OSMesa|hakios.gl" | head -5 >> "$out/start.txt"
echo "end: $(date -Is)" >> "$out/start.txt"

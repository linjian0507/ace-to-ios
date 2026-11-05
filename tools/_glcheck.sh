#!/bin/bash
cd /root/hakios || exit 1
echo "--- build log tail ---"
tail -20 /mnt/s/opencode/hakios/logs/acceptance-08-2026-11-05/build.log 2>&1
echo "--- start.txt ---"
cat /mnt/s/opencode/hakios/logs/acceptance-08-2026-11-05/start.txt 2>&1
echo "--- find libOSMesa ---"
find /root/hakios -name "libOSMesa*" -o -name "libhakios.gl.dylib" 2>/dev/null | head -10
echo "--- current root.apfs ---"
ls -la userland/root.apfs 2>&1
echo "--- mesa script? ---"
ls -la tools/mk-mesa.sh userland/lib 2>&1 | head

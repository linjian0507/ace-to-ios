#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/acceptance-07-2026-11-05
old=$out/root.apfs.before-07
new=/root/hakios/userland/root.apfs
python3 tools/apfs/apfs.py ls "$old" / 2>/dev/null | sed 's/^ *//' | awk '{print $NF, $(NF-3)}' | sort > /tmp/old.txt
python3 tools/apfs/apfs.py ls "$new" / 2>/dev/null | sed 's/^ *//' | awk '{print $NF, $(NF-3)}' | sort > /tmp/new.txt
echo "old entries: $(wc -l < /tmp/old.txt), new entries: $(wc -l < /tmp/new.txt)" > "$out/image-diff.txt"
echo "--- only in old (6 MiB) ---" >> "$out/image-diff.txt"
comm -23 /tmp/old.txt /tmp/new.txt | head -40 >> "$out/image-diff.txt"
echo "--- only in new (4 MiB) ---" >> "$out/image-diff.txt"
comm -13 /tmp/old.txt /tmp/new.txt | head -20 >> "$out/image-diff.txt"
cat "$out/image-diff.txt"
echo "--- LaunchDaemons old ---"
python3 tools/apfs/apfs.py ls "$old" /LaunchDaemons 2>/dev/null | awk '{print $NF}' | sort | head -40 > /tmp/oldjobs.txt
wc -l < /tmp/oldjobs.txt
echo "--- LaunchDaemons new ---"
python3 tools/apfs/apfs.py ls "$new" /LaunchDaemons 2>/dev/null | awk '{print $NF}' | sort | head -40 > /tmp/newjobs.txt
wc -l < /tmp/newjobs.txt
diff /tmp/oldjobs.txt /tmp/newjobs.txt | head -40

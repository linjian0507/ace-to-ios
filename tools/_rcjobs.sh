#!/bin/bash
cd /root/hakios || exit 1
echo "--- original 014918 /etc/rc.jobs ---"
python3 tools/apfs/apfs.py cat /mnt/s/opencode/hakios/logs/recovered-baseline-014918/ramdisk /etc/rc.jobs 2>&1 | head -30
echo "--- current root.apfs /etc/rc.jobs ---"
python3 tools/apfs/apfs.py cat userland/root.apfs /etc/rc.jobs 2>&1 | head -30
echo "--- sizes ---"
python3 tools/apfs/apfs.py ls /mnt/s/opencode/hakios/logs/recovered-baseline-014918/ramdisk /etc 2>&1 | head
python3 tools/apfs/apfs.py ls userland/root.apfs /etc 2>&1 | head

#!/bin/bash
cd /root/hakios || exit 1
echo "--- mtimes ---"
stat -c '%y %s %n' userland/system.apfs userland/obj/stage0 sysimage/system.manifest 2>&1
ls -ld sysimage/cryptex sysimage/cryptex-app 2>&1
echo "--- guard ---"
if [ obj/stage0 -nt userland/system.apfs ]; then echo "stage0 NEWER: would rebuild system"; else echo "stage0 older: system skipped"; fi
echo "--- current root ---"
ls -la userland/root.apfs

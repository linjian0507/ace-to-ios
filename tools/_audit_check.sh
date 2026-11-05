#!/bin/bash
cd /root/hakios || exit 1
echo "--- phone record ---"
ls -la /tmp/darwin-hakios-phone* 2>&1 | head
echo "--- record content ---"
cat /tmp/darwin-hakios-phone.ramdisk 2>&1
echo "--- sizes ---"
stat -c '%s %n' userland/system.apfs userland/root.apfs
printf 'system hex: %x\n' "$(stat -c %s userland/system.apfs)"
echo "resident from log: 94350000"
echo "--- xl ramdisk code ---"
sed -n '285,320p' xl
echo "--- runtime dir ---"
echo "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
ls -la "$XDG_RUNTIME_DIR"/darwin-hakios-phone* 2>&1

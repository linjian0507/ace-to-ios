#!/bin/bash
set -uo pipefail
cd /root/hakios || exit 1
out=/mnt/s/opencode/hakios/logs/delivery-2026-11-05
mkdir -p "$out"
cp -f hakios-kernel "$out/hakios-kernel"
cp -f xnuloader/loader.efi "$out/loader.efi"
cp -f /mnt/s/opencode/hakios/logs/acceptance-12-2026-11-05/root.apfs.default-lean "$out/root.apfs"
sha256sum "$out/hakios-kernel" "$out/loader.efi" "$out/root.apfs" > "$out/SHA256SUMS"
cat "$out/SHA256SUMS"
ls -la "$out"

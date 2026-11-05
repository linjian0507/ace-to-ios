#!/bin/bash
set -uo pipefail
cd /mnt/s/opencode/hakios || exit 1
echo "--- listing help ---"
python3 tools/hakios-config.py listing --help 2>&1 | head -20
echo "--- generate common_defconfig ---"
python3 tools/hakios-config.py listing common > configs/common_defconfig
echo "rc=$? lines=$(wc -l < configs/common_defconfig)"
head -6 configs/common_defconfig
echo "--- check ---"
python3 tools/hakios-config.py check --config configs/common_defconfig 2>&1 | tail -5

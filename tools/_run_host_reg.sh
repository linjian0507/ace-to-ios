#!/bin/bash
cd /root/hakios || exit 1
cp /mnt/s/opencode/hakios/tools/pclm10-host-regression.sh tools/
chmod +x tools/pclm10-host-regression.sh
bash tools/pclm10-host-regression.sh 2>&1 | tail -20
echo "rc=$?"

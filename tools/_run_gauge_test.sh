#!/bin/bash
cd /root/hakios || exit 1
cp /mnt/s/opencode/hakios/tools/pclm10-test-gauge-plausible.py tools/
python3 tools/pclm10-test-gauge-plausible.py 2>&1 | tail -5

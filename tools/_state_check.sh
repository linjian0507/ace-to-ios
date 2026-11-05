#!/bin/bash
cd /root/hakios || exit 1
echo "--- state ---"
./xl state 2>&1
echo "--- adb ---"
which adb; adb devices 2>&1 | head -5
echo "--- fastboot ---"
fastboot devices 2>&1
echo "--- battery via loader? ---"

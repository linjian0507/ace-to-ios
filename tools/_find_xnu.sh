#!/bin/bash
echo "--- find xnu source ---"
find /root -maxdepth 4 -type d -name "xnu-*" 2>/dev/null | head
find /root -maxdepth 5 -name "task_policy.c" 2>/dev/null | head
echo "--- apply.sh head ---"
sed -n '1,40p' /root/hakios/xnu/apply.sh
echo "--- disk ---"
ls -d /root/* 2>/dev/null | head -30

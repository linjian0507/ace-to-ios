#!/bin/bash
cd /root/hakios || exit 1
echo "--- xnu dir ---"
ls xnu | head -20
echo "--- grep escape ---"
grep -rn "escape" xnu/osfmk/kern --include=*.c --include=*.h 2>/dev/null | grep -i "monitor\|cpu" | head -10
echo "--- grep monitor in bsd/kern ---"
grep -rln "CPU monitor\|cpu monitor" xnu/osfmk xnu/bsd 2>/dev/null | head -10
echo "--- strings in kernel? ---"
strings hakios-kernel 2>/dev/null | grep -i "escape CPU" | head -3

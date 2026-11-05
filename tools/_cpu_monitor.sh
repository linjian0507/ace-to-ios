#!/bin/bash
cd /root/hakios || exit 1
echo "--- source search ---"
grep -rn "escape CPU monitor" xnu --include=*.c --include=*.h --include=*.cpp 2>/dev/null | head -10
echo "--- related ---"
grep -rn "CPU monitor" xnu/osfmk/kern xnu/bsd/kern --include=*.c --include=*.h 2>/dev/null | head -20

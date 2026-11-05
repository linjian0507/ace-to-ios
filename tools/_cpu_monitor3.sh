#!/bin/bash
cd /root/hakios/xnu-src/xnu-8796.121.2 || exit 1
echo "--- exact string ---"
grep -rn "escape CPU monitor" . --include=*.c --include=*.h --include=*.cpp 2>/dev/null | head
echo "--- cpu monitor ---"
grep -rn "CPU monitor" . --include=*.c --include=*.h 2>/dev/null | head -20
echo "--- proc_setpcontrol ---"
grep -rn "proc_setpcontrol\|P_CONTROL\|task_policy_set" bsd osfmk --include=*.c --include=*.h 2>/dev/null | head -20

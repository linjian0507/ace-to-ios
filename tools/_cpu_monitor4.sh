#!/bin/bash
cd /root/hakios/xnu-src/xnu-8796.121.2 || exit 1
echo "=== task_policy.c 2820-2905 ==="
sed -n '2820,2905p' osfmk/kern/task_policy.c
echo "=== kern_exec.c 4270-4310 ==="
sed -n '4270,4310p' bsd/kern/kern_exec.c

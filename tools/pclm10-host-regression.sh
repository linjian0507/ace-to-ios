#!/bin/bash
# pclm10 host regression: every test that runs without the phone.
# The device tests (thermal reads, touch, GPU, battery, haptics, torch) stay in the plan's
# device acceptance; this is the host evidence a combination regression can rely on.
cd "$(dirname "$0")/.." || exit 2
tests=(
    tools/pclm10-test-touch-ownership.py
    tools/pclm10-test-gpu-identity.py
    tools/pclm10-test-gpu-ring-log.py
    tools/pclm10-test-history-recovery.py
    tools/pclm10-test-payload-inventory.py
    tools/pclm10-test-interactive.py
    tools/pclm10-test-thermal-policy.py
    tools/pclm10-test-gauge-plausible.py
    tools/pclm10-test-hapc-recipe.py
    tools/pclm10-test-wled-apply.py
    tools/boot-args-test.py
    tools/kernel-version-test.py
    tools/mkesp-test.py
    tools/new-device-test.py
    tools/provenance-test.py
    tools/backup-partitions-test.py
)
pass=0
fail=0
for t in "${tests[@]}"; do
    if python3 "$t" > /tmp/pclm10-host-$(basename "$t").log 2>&1; then
        echo "PASS $t"
        pass=$((pass + 1))
    else
        echo "FAIL $t"
        tail -3 /tmp/pclm10-host-$(basename "$t").log | sed 's/^/     /'
        fail=$((fail + 1))
    fi
done
echo "pclm10 host regression: $pass passed, $fail failed"
[ "$fail" == 0 ]

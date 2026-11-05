#!/bin/bash
# Generate each phone test's defaults from the device defconfig, then apply
# explicit per-run overrides. Never boot an image through fastboot boot.
# Usage: tools/pclm10-run.sh [--dry-run] [name=value ...]
set -euo pipefail
cd "$(dirname "$0")/.."

dry=0
if [[ ${1:-} == --dry-run ]]; then
    dry=1
    shift
fi
defaults=$(python3 tools/hakios-config.py args --defconfig pclm10_defconfig)
read -r -a args <<< "$defaults"
for override in "$@"; do
    if [[ ! $override =~ ^[a-z][a-z0-9_]*=[a-zA-Z0-9_.:/,+@-]+$ ]]; then
        echo "pclm10-run: expected name=value, got '$override'" >&2
        exit 1
    fi
    key=${override%%=*}
    if [[ $key == bootcpu && $override != bootcpu=7 ]]; then
        echo 'pclm10-run: this phone requires bootcpu=7' >&2
        exit 1
    fi
    merged=()
    for existing in "${args[@]}"; do
        [[ ${existing%%=*} == "$key" ]] || merged+=("$existing")
    done
    args=("${merged[@]}" "$override")
done
[[ " ${args[*]} " == *' bootcpu=7 '* ]] || { echo 'pclm10-run: bootcpu=7 missing' >&2; exit 1; }
export XL_CMDLINE="${args[*]}"
echo "pclm10-run: effective boot arguments: $XL_CMDLINE"
[[ $dry == 0 ]] || exit 0
state=$(./xl state | tail -n 1)
if [[ $state != loader ]]; then
    echo "pclm10-run: device state '$state'; enter the installed loader before running" >&2
    exit 1
fi
export XL_SECS=${XL_SECS:-200}
export XL_HOME=fastboot
export XL_FOLLOW=${XL_FOLLOW:-10}
export XL_FOLLOW_KEEP=1
exec ./xl run

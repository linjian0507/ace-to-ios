#!/bin/bash
# Bounded RAM-only interaction check; NOT installation or a daily-use release.
set -euo pipefail
cd "$(dirname "$0")/.."
dry=()
case "${1:-}" in
    --dry-run) dry=(--dry-run); shift ;;
    "") ;;
    *) echo 'usage: tools/pclm10-interactive.sh [--dry-run]' >&2; exit 2 ;;
esac
[[ $# == 0 ]] || { echo 'interactive: arbitrary experiment overrides are not accepted' >&2; exit 2; }
if [[ ${#dry[@]} == 0 && -f devices/pclm10/configs/interactive-hold.txt ]]; then
    cat devices/pclm10/configs/interactive-hold.txt >&2
    echo 'interactive: HOLD; no device query, upload or run was attempted' >&2
    exit 3
fi
echo 'PCLM10 interaction check: RAM upload only; no flash, no persistent /var.'
echo '150-second limit, touch/buttons enabled; GPU recipe from logs/014918-run.log requested.'
echo 'Acceptance requires an Adreno renderer AND real HakiosGPU submissions; softpipe is a failure.'
echo 'Requires your matching resident system root and a GL=1 userland/root.apfs.'
echo 'Resident-root identity and visual usability are NOT certified by this script.'
if [[ ${#dry[@]} == 0 ]]; then
    for file in hakios-kernel xnuloader/loader.efi userland/root.apfs; do
        [[ -s $file ]] || { echo "interactive: required local artifact missing: $file" >&2; exit 1; }
    done
    # Check image contents, not merely that an old/small file exists. This does
    # not certify the identity of the large root resident in the phone's RAM.
    libs=$(python3 tools/apfs/apfs.py ls userland/root.apfs /lib)
    firmware=$(python3 tools/apfs/apfs.py ls userland/root.apfs /fw)
    for name in libhakios.gl.dylib libOSMesa.8.dylib; do
        grep -Fq " $name" <<< "$libs" || { echo "interactive: GL=1 image missing $name" >&2; exit 1; }
    done
    for name in a640_gmu.bin a630_sqe.fw a640_zap.mdt a640_zap.b02; do
        grep -Fq " $name" <<< "$firmware" || { echo "interactive: image missing GPU firmware $name" >&2; exit 1; }
    done
    sha256sum hakios-kernel xnuloader/loader.efi userland/root.apfs
fi
read -r -a gpu_args < devices/pclm10/configs/interactive-gpu.args
# Pin a known bounded flow, rather than inherit another experiment's environment.
export XL_SECS=150
export XL_RAMDISK=userland/root.apfs
run_args=(bootcpu=7 cpufreq=0 touch=1 pon=1
    thermal=1 thermal_pclm10=1 thermal_log=5 thermal_stop=60 "${gpu_args[@]}")
if [[ ${#dry[@]} != 0 ]]; then
    exec bash tools/pclm10-run.sh --dry-run "${run_args[@]}"
fi
mkdir -p logs
wrapper=$(mktemp logs/interactive-gpu-wrapper.XXXXXX)
bash tools/pclm10-run.sh "${run_args[@]}" | tee "$wrapper"
run_log=$(grep -oE 'logs/[0-9]+-run\.log' "$wrapper" | tail -1 || true)
[[ -n $run_log && -s $run_log ]] || {
    echo 'interactive: FAIL: no complete run log for GPU prerequisite check' >&2
    exit 1
}
python3 tools/pclm10-check-gpu-log.py "$run_log"
# Deliberately do not force gl=1: inject.c auto-loads GL when HakiosGPU exists.
# Unrelated probes remain absent from source defaults; avoid overflowing the
# 607-character boot line with redundant zeros and losing adreno_keep at its end.

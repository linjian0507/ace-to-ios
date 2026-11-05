#!/bin/bash
# Pull the phone's own firmware out of its Android into the fw-*/ directories at the top of the
# repository (never committed: it is the vendor's). The phone is on adb, in Android.
#
# The Reno Ace is an SM8150, so its files are not the reference phone's: the GPU (Adreno 640) names
# its firmware a630_* on CAF targets of this SoC, and Wi-Fi is a WCN3990, whose firmware runs on the
# ADSP rather than in a wpss of its own. So the GPU's files are found by listing the phone rather
# than by naming them: whatever /vendor/firmware holds beside gmu/sqe/zap is what the GPU driver will
# be given, whatever it turns out to be called. The ADSP's and modem's files are the CAF layout every
# Qualcomm Android of this generation has.
set -e
cd "$(dirname "$0")/../.."
pull() { # DIR SOURCE...  (missing files are told, not fatal)
	local dir=$1; shift
	mkdir -p "$dir"
	for f in "$@"; do adb pull "$f" "$dir/" >/dev/null 2>&1 || echo "pull-firmware.sh: $f: not on the phone"; done
	echo "pull-firmware.sh: $dir: $(ls -1 "$dir" | wc -l | tr -d ' ') files"
}
adb get-state >/dev/null

# the GPU: ask the phone what its files are called rather than assuming a630_ vs a660_
gpu=$(adb shell 'ls /vendor/firmware 2>/dev/null' | tr -d '\r' | grep -iE '^(a[0-9]{3}_(gmu\.bin|sqe\.fw|zap\.(mdt|b[0-9]{2})))$' || true)
if [ -n "$gpu" ]; then
	pull fw-gpu $(for f in $gpu; do echo /vendor/firmware/$f; done)
else
	echo "pull-firmware.sh: fw-gpu: no aXXX_gmu/sqe/zap under /vendor/firmware: check the GPU firmware layout (a6xx GPU firmware is signed per-SoC)"
fi

# the ADSP (audio, and the WCN3990's Wi-Fi firmware lives on it too) and the modem: CAF layout
pull fw-audio /vendor/firmware_mnt/image/adsp.mdt $(for i in $(seq -w 2 36); do echo /vendor/firmware_mnt/image/adsp.b$i; done)
pull fw-modem /vendor/firmware_mnt/image/modem.mdt $(for i in $(seq -w 0 28); do echo /vendor/firmware_mnt/image/modem.b$i; done)
mkdir -p fw-modem/image && adb pull /vendor/firmware_mnt/image fw-modem/ >/dev/null 2>&1 && echo "pull-firmware.sh: fw-modem/image: the modem's own files (TFTP)"

# Wi-Fi: a WCN3990 has no wpss of its own; its rampatch/NV live under /vendor/firmware/wlan
mkdir -p fw-wifi
adb pull /vendor/firmware/wlan fw-wifi/ >/dev/null 2>&1 && echo "pull-firmware.sh: fw-wifi/wlan: the WCN3990's rampatch and NV" || echo "pull-firmware.sh: /vendor/firmware/wlan: not on the phone (WCN3990: its firmware is on the ADSP; check the NV's place)"
for f in $(adb shell 'ls /vendor/firmware 2>/dev/null' | tr -d '\r' | grep -iE '^WCNSS' || true); do adb pull "/vendor/firmware/$f" fw-wifi/ >/dev/null 2>&1 || true; done

echo "pull-firmware.sh: done; Wi-Fi also wants fw-wifi/mac (the radio's address) and fw-wifi/network (the network's name and passphrase)"

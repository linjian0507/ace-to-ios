#!/bin/bash
# pull-firmware.sh, through root. On ColorOS the adb shell user can usually read /vendor/firmware and
# /vendor/firmware_mnt, but where it cannot (or the mount is fmask'd), this does the same pull by way
# of su: the firmware is copied, under root, to a world-readable staging directory in /data/local/tmp,
# pulled from there, and the staging directory is removed again. Nothing on the phone is changed.
# Needs the phone in Android with adb and a root that answers `su -c` (Magisk, KernelSU).
#
# The layout it leaves is pull-firmware.sh's: fw-gpu/, fw-wifi/, fw-audio/, fw-modem/. Camera
# firmware is not pulled: it is not needed for the modem or the network.
set -e
cd "$(dirname "$0")/../.."
S=/data/local/tmp/fw
adb get-state >/dev/null
# the GPU's files by listing the phone, as in pull-firmware.sh: the SM8150's Adreno 640 names them
# a630_* on CAF targets, but take whatever names the phone really has
gpu=$(adb shell "su -c 'ls /vendor/firmware'" | tr -d '\r' | grep -iE '^(a[0-9]{3}_(gmu\.bin|sqe\.fw|zap\.(mdt|b[0-9]{2})))$' || true)
gpu_cp=""
for f in $gpu; do gpu_cp="$gpu_cp /vendor/firmware/$f"; done
adb shell "su -c 'sh -c \"
S=$S; rm -rf \$S; mkdir -p \$S/gpu \$S/wifi \$S/audio \$S/modem
cp $gpu_cp \$S/gpu/ 2>/dev/null
cp /vendor/firmware_mnt/image/adsp.* \$S/audio/
cp /vendor/firmware_mnt/image/modem.* \$S/modem/
cp -r /vendor/firmware_mnt/image \$S/modem/
cp -r /vendor/firmware/wlan \$S/wifi/ 2>/dev/null
chmod -R a+rX \$S\"'"
rm -rf fw-stage
adb pull "$S" fw-stage >/dev/null
rm -rf fw-gpu fw-wifi fw-audio fw-modem
mv fw-stage/gpu fw-gpu; mv fw-stage/wifi fw-wifi; mv fw-stage/audio fw-audio; mv fw-stage/modem fw-modem; rmdir fw-stage
adb shell "su -c 'rm -rf $S'"
for d in fw-gpu fw-wifi fw-audio fw-modem fw-modem/image; do
	echo "pull-firmware-root.sh: $d: $(ls -1 "$d" | wc -l | tr -d ' ') files"
done
echo "pull-firmware-root.sh: done; Wi-Fi also wants fw-wifi/mac (the radio's address) and fw-wifi/network (the network's name and passphrase)"

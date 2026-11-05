#!/bin/bash
cd /root/hakios || exit 1
echo "--- wsl git ---"
git remote -v | head -2
git log --oneline -2
git status --short | wc -l
echo "--- wsl fetch ---"
git fetch origin main 2>&1 | tail -2
git log --oneline -1 origin/main
echo "--- hashes wsl ---"
sha256sum userland/build.sh userland/src/inject.c xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp configs/common_defconfig 2>/dev/null

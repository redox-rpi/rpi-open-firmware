#!/bin/bash
# Build bootcode.bin (with the embedded ARM chainloader) and the test payload
# using the toolchains from redox-rpi/build-toolchain.sh.
#
# Usage: redox-rpi/build.sh [TOOLCHAIN_WORKDIR]   (default: ../toolchains)
# Result: build/bootcode.bin, redox-rpi/payload/zImage

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(cd "${1:-$HERE/../toolchains}" && pwd)"
export PATH="$WORK/prefix/bin:$PATH"

cd "$HERE"
./buildall.sh
make -C redox-rpi/payload clean zImage
ls -l build/bootcode.bin arm_chainloader/build/arm_chainloader.bin redox-rpi/payload/zImage
sha256sum build/bootcode.bin redox-rpi/payload/zImage

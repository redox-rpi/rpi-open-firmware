#!/bin/bash
# Build librerpi/lk-overlay (the newer, LK-based open firmware) without Nix,
# with the toolchains from redox-rpi/build-toolchain.sh (vc4-elf-,
# arm-none-eabi-, aarch64-none-elf-), for the Pi 3B+ 64-bit test.
#
# Usage: redox-rpi/build-lk-overlay.sh [LK_DIR] [TOOLCHAIN_WORKDIR]
#   LK_DIR default ../lk-overlay (cloned and pinned if missing)
# Result: LK_DIR/build-vc4-stage1-sdonly/lk.bin (bootcode.bin),
#         LK_DIR/build-vc4-stage2/lk.elf, redox-rpi/payload64/Image-aarch64
# Then: redox-rpi/make-sdcard-lk.sh LK_DIR DTB OUT.img

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
LK="${1:-$HERE/../lk-overlay}"
WORK="$(cd "${2:-$HERE/../toolchains}" && pwd)"
export PATH="$WORK/prefix/bin:$PATH"
JOBS="${JOBS:-$(nproc)}"

LK_REPO=https://github.com/librerpi/lk-overlay.git
LK_REV=50c95c1                  # 2026-06-20

if [ ! -d "$LK" ]; then
  git clone "$LK_REPO" "$LK"
  git -C "$LK" checkout -q "$LK_REV"
fi
LK="$(cd "$LK" && pwd)"
cd "$LK"
git submodule update --init --depth 50 lk lib/tinyusb/upstream

# Upstream links libgcc from a fixed /nix/store path; ask the compiler instead.
sed -i 's|^ARCH_LDFLAGS += -L/nix/store/.*|ARCH_LDFLAGS += -L$(dir $(shell $(TOOLCHAIN_PREFIX)gcc -print-libgcc-file-name))|' \
  arch/vpu/rules.mk

# Our fixes for the Pi 3B+ (see the comments in each patch).
for p in "$HERE"/redox-rpi/lk-patches/*.patch; do
  if git apply --check "$p" 2>/dev/null; then
    git apply "$p"
  elif ! git apply --check -R "$p" 2>/dev/null; then
    echo "patch does not apply: $p" >&2; exit 1
  fi
done

# ARM programs that stage 2 embeds, one per chip. Only the BCM2837 one
# (rpi3-test, 64-bit) runs on a Pi 3B+. rpi2-test needs C++ headers our
# minimal arm-none-eabi GCC lacks, so it gets a placeholder that is never
# started on a BCM2837.
# rpi3-test ships without an SD driver (CONFIG_SD_BOOT := 0), so its loader
# cannot find sdhostp1 (Pi test #253: "mount failure: -2"); turn it on.
make PROJECT=rpi3-test TOOLCHAIN_PREFIX=aarch64-none-elf- CONFIG_SD_BOOT=1 -j"$JOBS"
make PROJECT=rpi1-test TOOLCHAIN_PREFIX=arm-none-eabi- -j"$JOBS"
mkdir -p build-rpi2-test
[ -s build-rpi2-test/lk.bin ] || printf 'RPI2-PLACEHOLDER' > build-rpi2-test/lk.bin

# payload.S includes ../../../build-rpiN-test/lk.bin relative to an include
# directory three levels deep (the armstubs path under Nix).
mkdir -p .incbin-root/a/b
make PROJECT=vc4-stage2 TOOLCHAIN_PREFIX=vc4-elf- \
  "GLOBAL_ASMFLAGS=-DASSEMBLY -Wa,-I$LK/.incbin-root/a/b" -j"$JOBS"
make PROJECT=vc4-stage1-sdonly TOOLCHAIN_PREFIX=vc4-elf- -j"$JOBS"

make -C "$HERE/redox-rpi/payload64" clean Image-aarch64

ls -l build-vc4-stage1-sdonly/lk.bin build-vc4-stage2/lk.elf build-rpi3-test/lk.bin

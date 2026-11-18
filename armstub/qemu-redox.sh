#!/bin/bash
# Boot a Redox SD card image in QEMU raspi3b through our armstub8 instead of
# QEMU's own boot code: stub at 0, U-Boot at 0x80000, devicetree at 32 MB
# with the fix-ups a firmware must do (memory size, console on the PL011,
# SD card on the Arasan controller). Without -kernel, QEMU starts all four
# cores at 0 in EL3, as the real Pi does.
#
# Usage: armstub/qemu-redox.sh IMAGE[.gz] U-BOOT.bin MAINLINE.dtb [SECONDS]
#   MAINLINE.dtb  bcm2837-rpi-3-b-plus.dtb from the U-Boot build
#                 (redox-rpi build/u-boot/dtb/)
# Works in WORK (default ./qemu-work); look for "4 CPUs online" and
# "devshd: listening" in the output.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
IMAGE="$1" UBOOT="$2" DTB="$3" SECS="${4:-300}"
WORK="${WORK:-qemu-work}"
mkdir -p "$WORK"

cp "$DTB" "$WORK/redox.dtb"
fdtput -t s "$WORK/redox.dtb" /chosen stdout-path "serial0:115200n8"
fdtput -t s "$WORK/redox.dtb" /soc/mmc@7e202000 status disabled
fdtput -t x "$WORK/redox.dtb" /memory@0 reg 0 3b400000	# 948 MiB, gpu_mem=16

# the devicetree address goes where a firmware writes it (dtb_ptr32, 0xf8)
python3 -I -c 'import struct, sys
b = bytearray(open(sys.argv[1], "rb").read())
struct.pack_into("<I", b, 0xf8, 0x2000000)
open(sys.argv[2], "wb").write(b)' "$HERE/build/armstub8.bin" "$WORK/armstub8.bin"

case "$IMAGE" in
*.gz) gunzip -c "$IMAGE" > "$WORK/run.img" ;;
*) cp "$IMAGE" "$WORK/run.img" ;;
esac
# QEMU's SD card must be a power of two in size
size=$(stat -c %s "$WORK/run.img")
sd=1
while [ "$sd" -lt "$size" ]; do sd=$((sd * 2)); done
truncate -s "$sd" "$WORK/run.img"

exec timeout "$SECS" qemu-system-aarch64 -M raspi3b -smp 4 -m 1024 \
	-nographic -monitor none -serial stdio -serial null \
	-device loader,file="$WORK/armstub8.bin",addr=0x0 \
	-device loader,file="$UBOOT",addr=0x80000 \
	-device loader,file="$WORK/redox.dtb",addr=0x2000000 \
	-drive file="$WORK/run.img",if=sd,format=raw -usb -device usb-kbd

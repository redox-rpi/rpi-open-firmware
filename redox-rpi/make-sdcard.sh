#!/bin/bash
# Build a small SD card image for the Pi 3B+ test, without root:
# one FAT16 partition with
#   bootcode.bin  this firmware (VPU part + embedded ARM chainloader)
#   rpi.dtb       Linux devicetree the chainloader patches (bootargs, memory)
#   cmdline.txt   kernel command line, read by the chainloader
#   zImage        what the chainloader jumps to: the test payload by default
#
# Usage: redox-rpi/make-sdcard.sh DTB OUT.img [ZIMAGE]
#   DTB    e.g. bcm2710-rpi-3-b-plus.dtb from the Raspberry Pi firmware
#   ZIMAGE default: redox-rpi/payload/zImage

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
DTB="$1"
OUT="$2"
ZIMAGE="${3:-$HERE/redox-rpi/payload/zImage}"
BOOTCODE="$HERE/build/bootcode.bin"

for f in "$BOOTCODE" "$DTB" "$ZIMAGE"; do
  [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

SIZE_MIB=34
START=2048                      # 1 MiB
SECTORS=$(( (SIZE_MIB - 2) * 2048 ))

rm -f "$OUT"
truncate -s ${SIZE_MIB}M "$OUT"
echo "start=$START, size=$SECTORS, type=e, bootable" | sfdisk -q "$OUT"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "console=ttyAMA0,115200 earlycon" > "$TMP/cmdline.txt"

IMG="$OUT@@$((START * 512))"
mformat -i "$IMG" -T $SECTORS -h 64 -s 32 -H $START -v OPENFW ::
mcopy -i "$IMG" "$BOOTCODE" ::bootcode.bin
mcopy -i "$IMG" "$DTB" ::rpi.dtb
mcopy -i "$IMG" "$TMP/cmdline.txt" ::cmdline.txt
mcopy -i "$IMG" "$ZIMAGE" ::zImage
mdir -i "$IMG" ::

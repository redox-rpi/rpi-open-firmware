#!/bin/bash
# SD card image for librerpi/lk-overlay (64-bit test), without root,
# in the layout its stage 1 and ARM loader expect:
#   partition 1, FAT:  bootcode.bin      (vc4-stage1-sdonly lk.bin)
#   partition 2, ext4: /boot/lk.elf      (vc4-stage2, runs on the VPU and
#                                         starts the ARM in 64-bit mode)
#                      /boot/Image-aarch64, /boot/rpi3.dtb, /boot/cmdline.txt
#                                        (read by the ARM linux-bootloader)
#
# Usage: redox-rpi/make-sdcard-lk.sh LK_OVERLAY_DIR DTB OUT.img [IMAGE]
#   IMAGE  default: redox-rpi/payload64/Image-aarch64 (the test payload)

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
LK="$1"
DTB="$2"
OUT="$3"
IMAGE="${4:-$HERE/redox-rpi/payload64/Image-aarch64}"
STAGE1="$LK/build-vc4-stage1-sdonly/lk.bin"
STAGE2="$LK/build-vc4-stage2/lk.elf"

for f in "$STAGE1" "$STAGE2" "$DTB" "$IMAGE"; do
  [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

SIZE_MIB=64
FAT_START=2048                  # 1 MiB
FAT_SECTORS=$(( 32 * 2048 ))    # 32 MiB
EXT_START=$(( FAT_START + FAT_SECTORS ))
EXT_SECTORS=$(( SIZE_MIB * 2048 - EXT_START ))

rm -f "$OUT"
truncate -s ${SIZE_MIB}M "$OUT"
sfdisk -q "$OUT" <<EOF
label: dos
start=$FAT_START, size=$FAT_SECTORS, type=c
start=$EXT_START, size=$EXT_SECTORS, type=83
EOF

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

FAT="$OUT@@$((FAT_START * 512))"
mformat -i "$FAT" -T $FAT_SECTORS -h 64 -s 32 -H $FAT_START -v FIRMWARE ::
mcopy -i "$FAT" "$STAGE1" ::bootcode.bin

mkdir -p "$TMP/root/boot"
cp "$STAGE2" "$TMP/root/boot/lk.elf"
cp "$IMAGE" "$TMP/root/boot/Image-aarch64"
cp "$DTB" "$TMP/root/boot/rpi3.dtb"
echo "console=ttyAMA0,115200 earlycon" > "$TMP/root/boot/cmdline.txt"
# LK's ext2 driver knows extents, 64bit, flex_bg, filetype; leave out newer
# features of current e2fsprogs it does not name
mkfs.ext4 -q -F -L root -O ^has_journal,^metadata_csum,^metadata_csum_seed,^orphan_file -d "$TMP/root" "$TMP/ext.img" $(( EXT_SECTORS / 2 ))k
dd if="$TMP/ext.img" of="$OUT" bs=512 seek=$EXT_START conv=notrunc status=none

mdir -i "$FAT" ::
debugfs -R "ls -l /boot" "$TMP/ext.img" 2>/dev/null

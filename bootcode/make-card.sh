#!/bin/bash
# SD card image for a test of our bootcode.bin, without root: one FAT16
# partition holding only build/bootcode.bin (M1 loads nothing else).
#
# Usage: bootcode/make-card.sh OUT.img

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$1"
BOOTCODE="$HERE/build/bootcode.bin"
[ -f "$BOOTCODE" ] || { echo "missing $BOOTCODE, run make first" >&2; exit 1; }

SIZE_MIB=34
START=2048                      # 1 MiB
SECTORS=$(( (SIZE_MIB - 2) * 2048 ))

rm -f "$OUT"
truncate -s ${SIZE_MIB}M "$OUT"
echo "start=$START, size=$SECTORS, type=e, bootable" | sfdisk -q "$OUT"

IMG="$OUT@@$((START * 512))"
mformat -i "$IMG" -T $SECTORS -h 64 -s 32 -H $START -v RBOOTCODE ::
mcopy -i "$IMG" "$BOOTCODE" ::bootcode.bin
mdir -i "$IMG" ::
sha256sum "$BOOTCODE"

#!/bin/bash
# SD card image for a test of our bootcode.bin, without root: one FAT16
# partition holding only build/bootcode.bin (M1 loads nothing else).
# With BASE.img: a copy of that card image with its bootcode.bin replaced
# by ours (for the monitor build on lk-overlay's card, so lk-overlay's
# stage 1 can be chained over serial and find its stage 2 on the card).
#
# Usage: bootcode/make-card.sh OUT.img [BASE.img]

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$1"
BOOTCODE="$HERE/build/bootcode.bin"
[ -f "$BOOTCODE" ] || { echo "missing $BOOTCODE, run make first" >&2; exit 1; }

if [ -n "${2:-}" ]; then
	cp "$2" "$OUT"
	OFF=$(( $(sfdisk -d "$OUT" | awk -F'start=|,' '/^[^#].*start=/{print $2+0; exit}') * 512 ))
	mcopy -o -i "$OUT@@$OFF" "$BOOTCODE" ::bootcode.bin
	mdir -i "$OUT@@$OFF" ::
	sha256sum "$BOOTCODE"
	exit 0
fi

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

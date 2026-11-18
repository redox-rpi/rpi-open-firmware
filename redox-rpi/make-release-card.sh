#!/bin/bash
# Card image for a Pi 3B+ that boots with our firmware only: a copy of a
# redox-rpi SD card image (BASE.img) with Broadcom's boot files taken out
# and ours put in. No root needed (mtools, sfdisk, fdtoverlay, fdtget).
#
#   bootcode.bin        ours, release build (cd bootcode && make BOOT=1)
#   armstub8-redox.bin  ours (cd armstub && make)
#   kernel8.img         U-Boot, a copy of the base card's u-boot.bin
#   redox-3bp.dtb       the base card's bcm2710-rpi-3-b-plus.dtb with the
#                       disable-bt and mmc overlays applied (make-dtb.sh)
#   removed             start*.elf, fixup*.dat, LICENCE.broadcom
#
# The rest of the card (U-Boot's boot.scr, the Redox partition) stays as it
# is. Usage: redox-rpi/make-release-card.sh BASE.img OUT.img

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BASE="${1:?usage: make-release-card.sh BASE.img OUT.img}"
OUT="${2:?usage: make-release-card.sh BASE.img OUT.img}"
BOOTCODE="$HERE/bootcode/build/bootcode.bin"
STUB="$HERE/armstub/build/armstub8.bin"

for f in "$BOOTCODE" "$STUB"; do
	[ -f "$f" ] || { echo "missing $f, build it first" >&2; exit 1; }
done
grep -q "release, boots from the SD card" "$BOOTCODE" ||
	{ echo "$BOOTCODE is not the release build (make BOOT=1)" >&2; exit 1; }

cp "$BASE" "$OUT"
OFF=$(( $(sfdisk -d "$OUT" | awk -F'start=|,' '/^[^#].*start=/{print $2+0; exit}') * 512 ))
IMG="$OUT@@$OFF"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/fw/overlays"
mcopy -i "$IMG" ::bcm2710-rpi-3-b-plus.dtb "$TMP/fw/"
mcopy -i "$IMG" ::overlays/disable-bt.dtbo ::overlays/mmc.dtbo "$TMP/fw/overlays/"
"$HERE/redox-rpi/make-dtb.sh" "$TMP/fw" "$TMP/redox-3bp.dtb"
mcopy -i "$IMG" ::u-boot.bin "$TMP/kernel8.img"

for f in start.elf start_cd.elf start_x.elf start_db.elf \
	fixup.dat fixup_cd.dat fixup_x.dat fixup_db.dat LICENCE.broadcom; do
	mdel -i "$IMG" "::$f" 2>/dev/null || true
done
mcopy -o -i "$IMG" "$BOOTCODE" ::bootcode.bin
mcopy -o -i "$IMG" "$STUB" ::armstub8-redox.bin
mcopy -o -i "$IMG" "$TMP/kernel8.img" ::kernel8.img
mcopy -o -i "$IMG" "$TMP/redox-3bp.dtb" ::redox-3bp.dtb

mdir -i "$IMG" ::
sha256sum "$BOOTCODE" "$STUB" "$TMP/redox-3bp.dtb"

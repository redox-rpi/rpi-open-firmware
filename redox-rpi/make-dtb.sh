#!/bin/sh
# Make redox-3bp.dtb (or redox-zero2w.dtb) for the bootcode's boot64:
# Broadcom's devicetree with the overlays of redox-rpi's config/config.txt
# applied (disable-bt: PL011 on GPIO 14/15 as serial0; mmc: the SD slot on
# the Arasan controller). Broadcom's start.elf applies them at boot; our
# VPU code does not, so we merge them on the host.
#
# For the direct boot without U-Boot (rpi-open-firmware #31) the blob also
# gets /chosen linux,initrd-start and linux,initrd-end as zeros; boot64
# fills them in when the card has an initramfs (fdt.c only overwrites in
# place). On by default for the Zero 2 W, DIRECT=1 for the 3B+.
#
# Usage: redox-rpi/make-dtb.sh FIRMWARE_DIR [OUT] [3bp|zero2w]
#   FIRMWARE_DIR holds bcm2710-rpi-3-b-plus.dtb, bcm2710-rpi-zero-2-w.dtb
#   and overlays/ (redox-rpi's build/firmware/<version>/, from
#   scripts/rpi-firmware.sh, or the boot partition of a redox-rpi card).
# Needs fdtoverlay, fdtget and fdtput (Debian package device-tree-compiler).
set -eu
fw=${1:?usage: make-dtb.sh FIRMWARE_DIR [OUT] [3bp|zero2w]}
board=${3:-3bp}
case $board in
3bp) in=bcm2710-rpi-3-b-plus.dtb; out=${2:-redox-3bp.dtb}; direct=${DIRECT:-0} ;;
zero2w) in=bcm2710-rpi-zero-2-w.dtb; out=${2:-redox-zero2w.dtb}; direct=${DIRECT:-1} ;;
*) echo "make-dtb: board $board: 3bp or zero2w" >&2; exit 1 ;;
esac
fdtoverlay -i "$fw/$in" -o "$out" "$fw/overlays/disable-bt.dtbo" "$fw/overlays/mmc.dtbo"
serial0=$(fdtget "$out" /aliases serial0)
[ "$serial0" = /soc/serial@7e201000 ] || { echo "make-dtb: serial0 is $serial0, expected the PL011" >&2; exit 1; }
[ "$(fdtget -t s "$out" /soc/mmc@7e300000 status)" = okay ] || { echo "make-dtb: mmc overlay not applied" >&2; exit 1; }
note=""
if [ "$direct" = 1 ]; then
	fdtput -c "$out" /chosen 2>/dev/null || true
	fdtput -t x "$out" /chosen linux,initrd-start 0
	fdtput -t x "$out" /chosen linux,initrd-end 0
	note=", initrd placeholders in /chosen"
fi
echo "$out: $(wc -c <"$out") bytes ($in), serial0 = PL011, SD on mmc@7e300000$note"

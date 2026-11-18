#!/bin/sh
# Make redox-3bp.dtb for the bootcode monitor's boot64: Broadcom's Pi 3B+
# devicetree with the overlays of redox-rpi's config/config.txt applied
# (disable-bt: PL011 on GPIO 14/15 as serial0; mmc: the SD slot on the
# Arasan controller). Broadcom's start.elf applies them at boot; our VPU
# code does not, so we merge them on the host.
#
# Usage: redox-rpi/make-dtb.sh FIRMWARE_DIR [OUT]
#   FIRMWARE_DIR holds bcm2710-rpi-3-b-plus.dtb and overlays/ (redox-rpi's
#   build/firmware/<version>/, from scripts/rpi-firmware.sh).
# Needs fdtoverlay and fdtget (Debian package device-tree-compiler).
set -eu
fw=${1:?usage: make-dtb.sh FIRMWARE_DIR [OUT]}
out=${2:-redox-3bp.dtb}
fdtoverlay -i "$fw/bcm2710-rpi-3-b-plus.dtb" -o "$out" \
	"$fw/overlays/disable-bt.dtbo" "$fw/overlays/mmc.dtbo"
serial0=$(fdtget "$out" /aliases serial0)
[ "$serial0" = /soc/serial@7e201000 ] || { echo "make-dtb: serial0 is $serial0, expected the PL011" >&2; exit 1; }
[ "$(fdtget -t s "$out" /soc/mmc@7e300000 status)" = okay ] || { echo "make-dtb: mmc overlay not applied" >&2; exit 1; }
echo "$out: $(wc -c <"$out") bytes, serial0 = PL011, SD on mmc@7e300000"

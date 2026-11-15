#!/bin/bash
# A card image for a 64 MB SD card: our firmware (release build), U-Boot
# and a small Redox that boots to the login prompt on the serial console.
# Our firmware has no HDMI yet (M4), so the screen stays dark.
#
#   partition 1, FAT16, 3 MiB:
#     bootcode.bin        ours, release build (cd bootcode && make BOOT=1)
#     armstub8-redox.bin  ours (cd armstub && make)
#     kernel8.img         U-Boot, the base card's u-boot.bin
#     redox-3bp.dtb       the base card's devicetree with its overlays
#                         applied (make-dtb.sh)
#     boot.scr            the base card's boot menu (card or network)
#     EFI/BOOT/BOOTAA64.EFI  the Redox bootloader, from HARDDRIVE.img
#   partition 2, RedoxFS, 53 MiB (RFS_MIB): the root filesystem of
#     HARDDRIVE.img without the files below, cloned into the smaller size
#
# BASE.img is a redox-rpi SD card image with U-Boot (make-sdcard.sh, or the
# v0.1.0 release card). HARDDRIVE.img is redox-rpi's
# `scripts/build-image.sh --small` (upstream/redox/build/aarch64/small/).
# Usage: redox-rpi/make-small-card.sh BASE.img HARDDRIVE.img OUT.img
#
# Left out of the RedoxFS (about 16 MiB): drivers for PCI devices, which
# the Pi 3B+ does not have; static libraries and headers; libstdc++ (no
# program on the card links it); the kernel's second copy and symbols; the
# network bootloader. The bootloader loads usr/lib/boot/kernel and initfs.
#
# FAT16, not FAT12: our fat.c reads FAT16 and FAT32 only, and mformat makes
# FAT12 on small partitions unless the clusters are one sector.
#
# Needs mtools, sfdisk, partx, fusermount, fdtoverlay, fdtget and the
# RedoxFS host tools (redoxfs, redoxfs-clone) of the redox-rpi build,
# found next to HARDDRIVE.img (../../fstools/bin) or in REDOXFS_TOOLS. No
# root needed.

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
USAGE="usage: make-small-card.sh BASE.img HARDDRIVE.img OUT.img"
BASE="${1:?$USAGE}"
HDD="${2:?$USAGE}"
OUT="${3:?$USAGE}"
BOOTCODE="$HERE/bootcode/build/bootcode.bin"
STUB="$HERE/armstub/build/armstub8.bin"
BOOT_MIB=4        # MBR gap (1 MiB) and the FAT partition
RFS_MIB=${RFS_MIB:-53}
MAX_BYTES=60000000  # a 64 MB card holds a little more; keep a margin
TOOLS="${REDOXFS_TOOLS:-$(cd "$(dirname "$HDD")/../../fstools/bin" 2>/dev/null && pwd || true)}"

REMOVE=(
	usr/lib/drivers/xhcid usr/lib/drivers/ihdgd usr/lib/drivers/ihdad
	usr/lib/drivers/virtio-netd usr/lib/drivers/rtl8168d
	usr/lib/drivers/rtl8139d usr/lib/drivers/e1000d
	usr/lib/drivers/testnetd usr/lib/drivers/ixgbed
	usr/include
	usr/lib/boot/kernel.all usr/lib/boot/kernel.sym
	usr/lib/boot/bootloader-live.efi
)

for f in "$BOOTCODE" "$STUB"; do
	[ -f "$f" ] || { echo "missing $f, build it first" >&2; exit 1; }
done
grep -q "release, boots from the SD card" "$BOOTCODE" ||
	{ echo "$BOOTCODE is not the release build (make BOOT=1)" >&2; exit 1; }
for t in redoxfs redoxfs-clone; do
	[ -x "$TOOLS/$t" ] || { echo "no $t in '$TOOLS', set REDOXFS_TOOLS" >&2; exit 1; }
done

TMP=$(mktemp -d)
MNT="$TMP/mnt"
cleanup() {
	mountpoint -q "$MNT" 2>/dev/null && fusermount -u "$MNT"
	rm -rf "$TMP"
}
trap cleanup EXIT
mkdir -p "$MNT" "$TMP/fw/overlays"

mount_rfs() {
	"$TOOLS/redoxfs" "$1" "$MNT" >"$TMP/redoxfs.log" 2>&1 &
	for _ in $(seq 50); do mountpoint -q "$MNT" && return; sleep 0.1; done
	cat "$TMP/redoxfs.log" >&2
	echo "RedoxFS mount of $1 failed" >&2
	exit 1
}

# Files from the base card.
BOFF=$(( $(sfdisk -d "$BASE" | awk -F'start=|,' '/^[^#].*start=/{print $2+0; exit}') * 512 ))
mcopy -i "$BASE@@$BOFF" ::bcm2710-rpi-3-b-plus.dtb "$TMP/fw/"
mcopy -i "$BASE@@$BOFF" ::overlays/disable-bt.dtbo ::overlays/mmc.dtbo "$TMP/fw/overlays/"
mcopy -i "$BASE@@$BOFF" ::u-boot.bin "$TMP/kernel8.img"
mcopy -i "$BASE@@$BOFF" ::boot.scr "$TMP/boot.scr"
"$HERE/redox-rpi/make-dtb.sh" "$TMP/fw" "$TMP/redox-3bp.dtb"

# The Redox image: EFI partition 2 (bootloader), RedoxFS partition 3.
read -r efi_start _ < <(partx -g -o START,SECTORS -n 2 "$HDD")
read -r rfs_start rfs_sectors < <(partx -g -o START,SECTORS -n 3 "$HDD")
mcopy -i "$HDD@@$((efi_start * 512))" ::EFI/BOOT/BOOTAA64.EFI "$TMP/BOOTAA64.EFI"
dd if="$HDD" of="$TMP/root.rfs" bs=512 skip="$rfs_start" count="$rfs_sectors" status=none

mount_rfs "$TMP/root.rfs"
for p in "${REMOVE[@]}"; do rm -rf "${MNT:?}/$p"; done
rm -f "$MNT"/usr/lib/*.a "$MNT"/usr/lib/libstdc++*
used_kib=$(du -sk "$MNT" | cut -f1)
fusermount -u "$MNT"
wait

truncate -s "${RFS_MIB}M" "$TMP/small.rfs"
"$TOOLS/redoxfs-clone" "$TMP/root.rfs" "$TMP/small.rfs" | sed 's/^/redoxfs-clone: /'

# FAT16 with one sector per cluster (see above).
FAT_MIB=$((BOOT_MIB - 1))
truncate -s "${FAT_MIB}M" "$TMP/fat"
mformat -i "$TMP/fat" -c 1 -v REDOXBOOT ::
[ "$(dd if="$TMP/fat" bs=1 skip=54 count=5 status=none)" = FAT16 ] ||
	{ echo "mformat did not make FAT16" >&2; exit 1; }
mcopy -i "$TMP/fat" "$BOOTCODE" ::bootcode.bin
mcopy -i "$TMP/fat" "$STUB" ::armstub8-redox.bin
mcopy -i "$TMP/fat" "$TMP/kernel8.img" ::kernel8.img
mcopy -i "$TMP/fat" "$TMP/redox-3bp.dtb" ::redox-3bp.dtb
mcopy -i "$TMP/fat" "$TMP/boot.scr" ::boot.scr
mmd -i "$TMP/fat" ::EFI ::EFI/BOOT
mcopy -i "$TMP/fat" "$TMP/BOOTAA64.EFI" ::EFI/BOOT/BOOTAA64.EFI

rm -f "$OUT"
truncate -s "$((BOOT_MIB + RFS_MIB))M" "$OUT"
printf 'start=2048, size=%d, type=e, bootable\nstart=%d, type=da\n' \
	$((FAT_MIB * 2048)) $((BOOT_MIB * 2048)) | sfdisk -q "$OUT"
dd if="$TMP/fat" of="$OUT" bs=1M seek=1 conv=notrunc status=none
dd if="$TMP/small.rfs" of="$OUT" bs=1M seek="$BOOT_MIB" conv=notrunc status=none

size=$(stat -c %s "$OUT")
[ "$size" -le "$MAX_BYTES" ] || { echo "$OUT is $size bytes, more than $MAX_BYTES" >&2; exit 1; }
mdir -i "$OUT@@1048576" -/ ::
echo "RedoxFS: $((used_kib / 1024)) MiB of files in $RFS_MIB MiB"
echo "$OUT: $size bytes"
sha256sum "$BOOTCODE" "$STUB" "$TMP/redox-3bp.dtb"

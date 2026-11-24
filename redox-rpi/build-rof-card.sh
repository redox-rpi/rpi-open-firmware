#!/usr/bin/env bash
# The release card in one go (#84): our firmware plus the minimal Redox with
# quiet boot, one card for the Pi 3B+ and the Pi Zero 2 W (#56, #57). The
# Redox flags are fixed here, so a card never misses the minimal
# configuration:
#
#   scripts/build-image.sh --release-netboot --experiment quiet-boot \
#       --experiment boot-no-errors --experiment rof-test-autologin
#   scripts/console-image.sh --minimal --root-on-card
#                           (no network, sound, desktop or developer tools;
#                           USB keyboard; initfs without drivers for other
#                           hardware; root on the card, redox-rpi#615 A)
#
#   quiet-boot          HDMI shows the boot screen, then the console
#   boot-no-errors      no avoidable ERROR/WARN lines, kernel DEBUG lines
#                       off the serial line (#59, redox-rpi#556)
#   rof-test-autologin  root logged in on the HDMI console, a test card
#                       only (the serial console keeps its login prompt)
#
# Steps:
#   1. firmware: host tests, then the release build (make BOOT=1 HDMI=1);
#      the build ID is the version, with the firmware commit added when
#      the version does not name it
#   2. Redox, in the redox-rpi clone given with --redox (a clone of your
#      own: the build writes into its upstream/ and build/)
#   3. the card: a FAT32 partition with bootcode.bin, config.txt,
#      cmdline.txt (quiet, and the root partition's REDOXFS_UUID),
#      kernel8.img, initramfs (the initfs only), Broadcom's devicetrees
#      with the overlays merged for both boards (make-dtb.sh) and
#      LICENCE.broadcom; then the RedoxFS root partition
#   4. rof-card.txt on the card and next to the image: both commits, the
#      Redox build ID, the flags and the SHA-256 of every boot file
#   5. with --publish: the image server, swiftflash family redox-rpi-rof
#      (redox-rpi's scripts/publish-sdcard.sh, also as -latest, with the
#      Redox build manifest)
#
# Both trees must be clean (no uncommitted changes to tracked files), so
# the commits named are the ones built.
#
# Usage:
#   redox-rpi/build-rof-card.sh --redox <redox-rpi clone> [--version <v>]
#                               [--out <dir>] [--publish]
#
#   --redox    the redox-rpi clone to build Redox in, at the commit you want
#   --version  card version: v<major>.<minor>… (v0.1.0-rc4) or YYYYMMDD…;
#              default YYYYMMDD-<firmware commit>-<Redox commit>
#   --out      where the card and its files go (default build/rof-card in
#              this repository)
#   --publish  publish the card on the image server
#
# Environment: CROSS_COMPILE as for bootcode/Makefile.
# Needs: the redox-rpi build host (docs/build-host.md in redox-rpi), mtools,
# sfdisk, fdtoverlay, fdtget, fdtput, xz.

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
FAMILY=redox-rpi-rof
EXPERIMENTS=(quiet-boot boot-no-errors rof-test-autologin)
REDOX_FLAGS=(--release-netboot)
for e in "${EXPERIMENTS[@]}"; do REDOX_FLAGS+=(--experiment "$e"); done
FAT_MIB=63 # partition 1 from 1 MiB; the RedoxFS partition follows
redox=""
version=""
out="$HERE/build/rof-card"
publish=no

die() { echo "build-rof-card: $*" >&2; exit 1; }
while [ $# -gt 0 ]; do
	case "$1" in
	--redox) redox="$2"; shift 2 ;;
	--version) version="$2"; shift 2 ;;
	--out) out="$2"; shift 2 ;;
	--publish) publish=yes; shift ;;
	-h | --help) sed -n '2,/^$/s/^# \{0,1\}//p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
done
[ -n "$redox" ] || die "--redox <redox-rpi clone> is needed"
redox="$(cd "$redox" && pwd)"
[ -x "$redox/scripts/build-image.sh" ] && [ -x "$redox/scripts/console-image.sh" ] ||
	die "$redox is not a redox-rpi clone with console-image.sh"
for e in "${EXPERIMENTS[@]}"; do
	[ -d "$redox/patches/experiments/$e" ] || die "$redox has no experiment $e (too old?)"
done
for need in mcopy mmd mformat sfdisk fdtoverlay fdtget fdtput sha256sum; do
	command -v "$need" >/dev/null || die "$need missing"
done

clean() { git -C "$1" diff --quiet HEAD -- || die "$1 has uncommitted changes"; }
clean "$HERE"
clean "$redox"
fw_commit=$(git -C "$HERE" rev-parse --short=7 HEAD)
rx_commit=$(git -C "$redox" rev-parse --short=8 HEAD)
[ -n "$version" ] || version="$(date -u +%Y%m%d)-$fw_commit-$rx_commit"
[[ "$version" =~ ^(v[0-9]+\.[0-9]+|[0-9]{8})[A-Za-z0-9.+_-]*$ ]] ||
	die "version $version must start with v<major>.<minor> or YYYYMMDD"
name="$FAMILY-$version"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$out/work"
rm -rf "$work"
mkdir -p "$work/fat/overlays"
echo "== $name: firmware $fw_commit, Redox $rx_commit"

# 1. Firmware
echo "== firmware: host tests and release build"
(cd "$HERE/bootcode" &&
	bash tests/fdt-test.sh >/dev/null &&
	bash tests/config-test.sh >/dev/null &&
	bash tests/fat-test.sh >/dev/null) || die "a firmware host test fails"
make -s -C "$HERE/bootcode" clean >/dev/null 2>&1 || true
build_id="$version"
[[ "$version" = *"$fw_commit"* ]] || build_id="$version-$fw_commit"
make -s -C "$HERE/bootcode" BOOT=1 HDMI=1 BUILD_ID="$build_id" >"$work/fw-build.log" 2>&1 ||
	{ tail -20 "$work/fw-build.log" >&2; die "firmware build failed"; }
! grep -qi warning "$work/fw-build.log" || { grep -i warning "$work/fw-build.log" >&2; die "the release build has warnings"; }
bootcode="$HERE/bootcode/build/bootcode.bin"
# grep reads to the end (no -m1): with pipefail, an early exit could kill strings with SIGPIPE
banner=$(strings "$bootcode" | grep "redox-rpi bootcode" | sed -n 1p)
[ -n "$banner" ] || die "no banner in $bootcode"
[[ "$banner" = *"release, boots from the SD card, boot screen on HDMI"* ]] ||
	die "not the HDMI release build: $banner"

# 2. Redox, the minimal configuration
echo "== Redox: build-image.sh ${REDOX_FLAGS[*]}"
"$redox/scripts/build-image.sh" "${REDOX_FLAGS[@]}" >"$work/redox-build.log" 2>&1 ||
	{ tail -20 "$work/redox-build.log" >&2; die "Redox build failed (log $work/redox-build.log)"; }
rx_build=$(grep -o 'build v[^,]*' "$work/redox-build.log" | tail -1 | cut -d' ' -f2)
rx_manifest="$redox/build/manifests/release-netboot-$rx_build.toml"
[ -n "$rx_build" ] && [ -f "$rx_manifest" ] || die "no build manifest for build ${rx_build:-?} in $redox/build/manifests"
echo "== Redox: console-image.sh --minimal ($rx_build)"
"$redox/scripts/console-image.sh" --minimal --root-on-card \
	--image "$redox/upstream/redox/build/aarch64/release-netboot/harddrive.img" \
	--out "$work/console" >"$work/console.log" 2>&1 ||
	{ tail -20 "$work/console.log" >&2; die "console-image.sh failed (log $work/console.log)"; }
for e in "${EXPERIMENTS[@]}"; do
	grep -q "EXPERIMENTS=.*\\b$e\\b" "$work/console/manifest.txt" || die "the console image has no $e"
done
grep -q '^prune config/console-prune-minimal.txt' "$work/console/manifest.txt" || die "the console image is not --minimal"
grep -q '^prune_initfs ' "$work/console/manifest.txt" || die "the console image has the full initfs (redox-rpi too old?)"
root_words=$(cat "$work/console/cmdline-root.txt" 2>/dev/null) || die "no cmdline-root.txt: console-image.sh without --root-on-card?"

# 3. The card's FAT partition
echo "== card"
# shellcheck source=/dev/null
. "$redox/scripts/rpi-firmware.sh"
fetch_firmware "$redox" >/dev/null
fwc="$firmware_cache"
for f in bcm2710-rpi-3-b-plus.dtb bcm2710-rpi-3-b.dtb bcm2710-rpi-zero-2-w.dtb LICENCE.broadcom \
	overlays/disable-bt.dtbo overlays/mmc.dtbo; do
	cp "$fwc/$f" "$work/fat/$f"
done
# direct boot (no U-Boot): initrd and bootargs placeholders on both boards
DIRECT=1 "$HERE/redox-rpi/make-dtb.sh" "$work/fat" "$work/fat/redox-3bp.dtb" 3bp >/dev/null
"$HERE/redox-rpi/make-dtb.sh" "$work/fat" "$work/fat/redox-zero2w.dtb" zero2w >/dev/null
cp "$bootcode" "$work/fat/bootcode.bin"
cp "$work/console/kernel8.img" "$work/console/initramfs" "$work/fat/"
cat >"$work/fat/config.txt" <<EOF
# Open firmware release card ($name): Pi 3B+ and Pi Zero 2 W.
# Made by rpi-open-firmware redox-rpi/build-rof-card.sh.

# rpi-boot-stub with the Redox kernel, and the minimal Redox (initfs, then
# the root file system).
arm_64bit=1
kernel=kernel8.img
initramfs initramfs followkernel

# Serial console on GPIO14/15 (pins 8/10) through the PL011 UART.
enable_uart=1
dtoverlay=disable-bt

# SD card on the Arasan controller.
dtoverlay=mmc

gpu_mem=16

# Boot screen title of the open firmware.
boot_title=Redox minimal, open firmware $version
EOF
printf 'quiet RPI_FB=1280x720 %s\n' "$root_words" >"$work/fat/cmdline.txt"

{
	echo "# $name"
	echo "made $(date -u +%Y-%m-%dT%H:%M:%SZ) by redox-rpi/build-rof-card.sh"
	echo "firmware rpi-open-firmware $fw_commit ($(git -C "$HERE" log -1 --format=%s | cut -c1-72))"
	echo "firmware_banner $banner"
	echo "redox redox-rpi $rx_commit ($(git -C "$redox" log -1 --format=%s | cut -c1-72))"
	echo "redox_build $rx_build"
	echo "redox_flags build-image.sh ${REDOX_FLAGS[*]}; console-image.sh --minimal"
	echo "broadcom_files $(basename "$fwc")"
	echo "root partition 2, RedoxFS $(sha256sum "$work/console/console.fs" | cut -d' ' -f1), $root_words"
	grep -E '^(release (EXPERIMENTS|IMAGE)|prune )' "$work/console/manifest.txt"
	(cd "$work/fat" && sha256sum bootcode.bin config.txt cmdline.txt kernel8.img initramfs \
		redox-3bp.dtb redox-zero2w.dtb | sed 's/^/sha256 /')
} >"$work/fat/rof-card.txt"

img="$out/$name.img"
rm -f "$img"
root_bytes=$(stat -c %s "$work/console/console.fs")
truncate -s $(((1 + FAT_MIB) * 1048576 + root_bytes)) "$img"
printf 'start=2048, size=%d, type=c, bootable\nstart=%d, size=%d, type=83\n' \
	$((FAT_MIB * 2048)) $(((1 + FAT_MIB) * 2048)) $((root_bytes / 512)) | sfdisk -q "$img"
dd if="$work/console/console.fs" of="$img" bs=1M seek=$((1 + FAT_MIB)) conv=notrunc status=none
fat="$img@@1048576"
mformat -i "$fat" -F -v REDOXBOOT -T $((FAT_MIB * 2048)) ::
mmd -i "$fat" ::/overlays
mcopy -i "$fat" "$work/fat/overlays/"* ::/overlays/
for f in "$work/fat"/*; do
	[ -f "$f" ] && mcopy -i "$fat" "$f" ::/
done
for f in bootcode.bin config.txt cmdline.txt kernel8.img initramfs rof-card.txt; do
	mtype -i "$fat" "::/$f" | cmp -s - "$work/fat/$f" || die "$f on the card differs"
done
cp "$work/fat/rof-card.txt" "$out/$name.rof-card.txt"
echo "== $img"
cat "$work/fat/rof-card.txt"

# 5. Publish
if [ "$publish" = yes ]; then
	echo "== publish"
	args=(--family "$FAMILY" --version "$version" --owner open-firmware --commit "$fw_commit" --latest
		--manifest "$rx_manifest")
	"$redox/scripts/publish-sdcard.sh" "$img" "${args[@]}"
fi

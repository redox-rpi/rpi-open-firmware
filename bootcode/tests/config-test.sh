#!/bin/bash
# Host test for config.c (rof #48): the config.txt keys and filters of a
# direct boot, on redox-rpi's standard card config and on filter cases.
# Needs gcc.   bootcode/tests/config-test.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir "$t/inc"
{ echo '#pragma once'; echo '#include <stdint.h>'; echo '#include <stdbool.h>'; echo '#include <stdio.h>'
  sed -n '/^\/\* config.c:/,/^const char \*cfg_board_dtb/p' "$here/../hw.h"; } > "$t/inc/hw.h"
cp "$here/../config.c" "$t/"
gcc -O1 -Wall -Wextra -I"$t/inc" -o "$t/h" "$here/config-host.c" "$t/config.c"
fail=0
expect() { # name, file, board, levels, expected substring(s)...
	local name=$1 file=$2 board=$3 lev=$4; shift 4
	local out; out=$("$t/h" "$file" "$board" "$lev" | grep '^RESULT')
	for e in "$@"; do
		if [[ "$out" != *"$e"* ]]; then echo "FAIL $name: no '$e' in: $out"; fail=1; return; fi
	done
	echo "ok   $name"
}

# redox-rpi's direct card (scripts/make-sdcard.sh --direct)
cat > "$t/std.txt" <<'C'
# Raspberry Pi firmware configuration for Redox (Pi 3, 3B+, Zero 2 W).
arm_64bit=1
kernel=kernel8.img
initramfs initramfs followkernel

enable_uart=1
dtoverlay=disable-bt
dtoverlay=mmc
gpu_mem=16
boot_title=Redox
C
expect "standard card, 3B+" "$t/std.txt" d 0 "kernel=kernel8.img " "initramfs=initramfs " "armstub= " "dt= " "overlays=disable-bt,mmc " "boarddtb=bcm2710-rpi-3-b-plus.dtb" "64=1"
expect "standard card, Zero 2 W" "$t/std.txt" 12 0 "boarddtb=bcm2710-rpi-zero-2-w.dtb" "overlays=disable-bt,mmc "
printf '' > "$t/empty.txt"
expect "no config: defaults" "$t/empty.txt" d 0 "kernel=kernel8.img " "initramfs= " "cmdline=cmdline.txt " "overlays= "

cat > "$t/filt.txt" <<'C'
kernel=common.img
[pi4]
kernel=pi4.img
dtoverlay=pi4-only
[pi3]
kernel=pi3.img
[pi02]
kernel=zero2.img
initramfs zero.cpio 0x2000000
[all]
armstub=stub-all.bin
[gpio3=0]
kernel=fallback.img
[all]
[none]
device_tree=never.dtb
[all]
  cmdline = my-cmdline.txt   
dtoverlay=foo,bar=1
dtoverlay=
dtparam=audio=on
include extra.txt
C
expect "filters, 3B+, GPIO3 high" "$t/filt.txt" d 8 "kernel=pi3.img " "armstub=stub-all.bin" "dt= " "unknown=0" "cmdline=my-cmdline.txt " "overlays=foo "
expect "filters, Zero 2 W" "$t/filt.txt" 12 8 "kernel=zero2.img " "initramfs=zero.cpio "
expect "filters, 3B+, GPIO3 low" "$t/filt.txt" d 0 "kernel=fallback.img "
expect "filters, unknown board" "$t/filt.txt" 99 8 "kernel=common.img "
printf '[tryboot]\nkernel=try.img\n[all]\narm_64bit=0\ndevice_tree=\nramfsfile=r.img\n' > "$t/unk.txt"
expect "unknown filter skipped, empty device_tree" "$t/unk.txt" d 0 "kernel=kernel8.img " "unknown=1" "64=0" "dt=set: " "initramfs=r.img "
printf 'kernel=%s\n' "$(printf 'x%.0s' {1..40})" > "$t/long.txt"
expect "too long value ignored" "$t/long.txt" d 0 "kernel=kernel8.img "
[ $fail = 0 ] && echo "all ok"
exit $fail

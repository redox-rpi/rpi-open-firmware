#!/bin/bash
# Write armstub8-builtin.h for the bootcode from armstub8.bin (rof #48):
#   bootcode/tests/armstub-header.sh armstub/build/armstub8.bin > bootcode/armstub8-builtin.h
# With --check, compare the header with the one in bootcode/ and fail when
# they differ (run after changing armstub/armstub8.S).
set -euo pipefail
check=no
[ "${1:-}" = --check ] && { check=yes; shift; }
bin=${1:?usage: armstub-header.sh [--check] armstub8.bin}
[ "$(wc -c < "$bin")" -eq 256 ] || { echo "armstub8.bin is not 256 bytes" >&2; exit 1; }
gen() {
	cat <<H
/*
 * The built-in armstub8 (rof #48): armstub/armstub8.S, 256 bytes, used when
 * the card has no armstub= in config.txt and no armstub8-redox.bin, as
 * start.elf uses its own. Made by \`make armstub-header\` in bootcode/ from
 * armstub/build/armstub8.bin (tests/armstub-header.sh).
 * sha256 $(sha256sum < "$bin" | cut -d' ' -f1)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
static const uint8_t armstub8_builtin[256] = {
H
	od -An -v -tx1 -w8 "$bin" | sed -E 's/ ([0-9a-f]{2})/ 0x\1,/g; s/^ /  /; $ s/,$//'
	echo "};"
}
if [ $check = yes ]; then
	here=$(cd "$(dirname "$0")/.." && pwd)
	diff <(gen) "$here/armstub8-builtin.h" && echo "ok   armstub8-builtin.h matches $bin"
else
	gen
fi

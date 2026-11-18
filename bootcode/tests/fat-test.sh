#!/bin/bash
# Host test for fat.c: FAT16 and FAT32 card images (mtools, sfdisk), long
# and 8.3 names, case-insensitive lookup, files over many clusters; every
# loaded file must equal the original. Needs gcc, mtools and sfdisk.
#   bootcode/tests/fat-test.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir "$t/inc" "$t/src" "$t/out"
cat > "$t/inc/hw.h" <<'H'
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
struct sd_card { bool ready, high_capacity; uint32_t mb; char name[6]; };
extern struct sd_card g_sd;
bool sd_init(void);
bool sd_read(uint32_t lba, uint32_t *buf);
bool fat_list(void);
int32_t fat_load(const char *name, uint32_t addr, uint32_t max);
H
# on the host, fat_load's target address is an offset into hostbuf
sed 's|uint8_t \*dst = (uint8_t \*)addr;|extern uint8_t hostbuf[]; uint8_t *dst = hostbuf + addr;|' \
	"$here/../fat.c" > "$t/fat.c"
gcc -O1 -Wall -Wextra -I"$t/inc" -o "$t/fat-host" "$here/fat-host.c" "$t/fat.c"
head -c 3000000 /dev/urandom > "$t/src/Big-File.Data"
head -c 256 /dev/urandom > "$t/src/armstub8-redox.bin"
head -c 70000 /dev/urandom > "$t/src/u-boot.bin"
echo "arm_64bit=1" > "$t/src/config.txt"
fail=0
for fat in 16 32; do
	secs=$([ $fat = 16 ] && echo 131072 || echo 614400)
	truncate -s $((secs * 512)) "$t/p.img"
	if [ $fat = 16 ]; then mformat -i "$t/p.img" -T $secs -h 64 -s 32 -c 4 ::
	else mformat -i "$t/p.img" -T $secs -h 64 -s 32 -F -c 1 ::; fi
	for f in "$t"/src/*; do mcopy -i "$t/p.img" "$f" "::$(basename "$f")"; done
	rm -f "$t/d.img"; truncate -s 1M "$t/d.img"; cat "$t/p.img" >> "$t/d.img"
	printf 'label: dos\nstart=2048, type=%s\n' "$([ $fat = 16 ] && echo 6 || echo c)" | sfdisk -q "$t/d.img"
	rm -f "$t"/out/*
	"$t/fat-host" "$t/d.img" "$t/out" big-file.data armstub8-redox.bin U-BOOT.BIN CONFIG.TXT >/dev/null
	for pair in Big-File.Data:big-file.data armstub8-redox.bin:armstub8-redox.bin u-boot.bin:U-BOOT.BIN config.txt:CONFIG.TXT; do
		if cmp -s "$t/src/${pair%%:*}" "$t/out/${pair##*:}"; then echo "FAT$fat ok   ${pair%%:*}"
		else echo "FAT$fat FAIL ${pair%%:*}"; fail=1; fi
	done
done
exit $fail

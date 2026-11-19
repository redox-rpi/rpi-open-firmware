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
#define SD_RUN_MAX 8u /* small, so files take many runs */
bool sd_read_blocks(uint32_t lba, uint32_t n, uint32_t *buf);
extern void (*g_fat_progress)(uint32_t done, uint32_t total);
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
# a subdirectory, as overlays/ on the release card (rof #48), with more
# entries than one cluster of the FAT32 image holds
head -c 1221 /dev/urandom > "$t/mmc.dtbo"
head -c 9000 /dev/urandom > "$t/deep.bin"
fail=0
for fat in 16 32; do
	secs=$([ $fat = 16 ] && echo 131072 || echo 614400)
	truncate -s $((secs * 512)) "$t/p.img"
	if [ $fat = 16 ]; then mformat -i "$t/p.img" -T $secs -h 64 -s 32 -c 4 ::
	else mformat -i "$t/p.img" -T $secs -h 64 -s 32 -F -c 1 ::; fi
	for f in "$t"/src/*; do mcopy -i "$t/p.img" "$f" "::$(basename "$f")"; done
	mmd -i "$t/p.img" ::overlays ::overlays/sub
	for i in $(seq 1 30); do echo $i > "$t/x"; mcopy -i "$t/p.img" "$t/x" "::overlays/long-name-number-$i.dtbo"; done
	mcopy -i "$t/p.img" "$t/mmc.dtbo" ::overlays/mmc.dtbo
	mcopy -i "$t/p.img" "$t/deep.bin" ::overlays/sub/Deep.Bin
	rm -f "$t/d.img"; truncate -s 1M "$t/d.img"; cat "$t/p.img" >> "$t/d.img"
	printf 'label: dos\nstart=2048, type=%s\n' "$([ $fat = 16 ] && echo 6 || echo c)" | sfdisk -q "$t/d.img"
	rm -rf "$t"/out/*
	"$t/fat-host" "$t/d.img" "$t/out" big-file.data armstub8-redox.bin U-BOOT.BIN CONFIG.TXT >/dev/null
	for pair in Big-File.Data:big-file.data armstub8-redox.bin:armstub8-redox.bin u-boot.bin:U-BOOT.BIN config.txt:CONFIG.TXT; do
		if cmp -s "$t/src/${pair%%:*}" "$t/out/${pair##*:}"; then echo "FAT$fat ok   ${pair%%:*}"
		else echo "FAT$fat FAIL ${pair%%:*}"; fail=1; fi
	done
	mkdir -p "$t/out/overlays/sub"
	"$t/fat-host" "$t/d.img" "$t/out" overlays/mmc.dtbo overlays/sub/DEEP.BIN overlays/missing.dtbo mmc.dtbo >/dev/null
	if cmp -s "$t/mmc.dtbo" "$t/out/overlays/mmc.dtbo" && cmp -s "$t/deep.bin" "$t/out/overlays/sub/DEEP.BIN"; then
		echo "FAT$fat ok   subdirectories"
	else echo "FAT$fat FAIL subdirectories"; fail=1; fi
	if [ ! -e "$t/out/overlays/missing.dtbo" ] && [ ! -e "$t/out/mmc.dtbo" ]; then echo "FAT$fat ok   missing files and files only in a subdirectory not found"
	else echo "FAT$fat FAIL found a file that is not there"; fail=1; fi
done
exit $fail

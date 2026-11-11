/*
 * Host test for fat.c: reads a disk image file instead of the SD card.
 * Built and run by fat-test.sh. SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include <stdlib.h>
#include <string.h>

struct sd_card g_sd = {true, false, 0, ""};
uint8_t hostbuf[64 << 20];
static FILE *img;

bool sd_init(void) { return true; }
bool sd_read(uint32_t lba, uint32_t *buf) {
	fseek(img, (long)lba * 512, SEEK_SET);
	return fread(buf, 512, 1, img) == 1;
}

int main(int argc, char **argv) {
	img = fopen(argv[1], "rb");
	if (!img)
		return 2;
	fat_list();
	for (int i = 3; i < argc; i++) {
		int32_t n = fat_load(argv[i], 0, sizeof hostbuf);
		if (n < 0)
			continue;
		char p[512];
		snprintf(p, sizeof p, "%s/%s", argv[2], argv[i]);
		FILE *f = fopen(p, "wb");
		fwrite(hostbuf, 1, (size_t)n, f);
		fclose(f);
	}
	return 0;
}

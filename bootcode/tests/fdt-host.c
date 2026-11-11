/*
 * Host test for fdt.c: patch the memory node of a devicetree file and
 * write the result. Built and run by fdt-test.sh.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include <stdlib.h>

int main(int argc, char **argv) {
	static uint8_t buf[1 << 20];
	if (argc != 4)
		return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	size_t n = fread(buf, 1, sizeof buf, f);
	fclose(f);
	if (!fdt_set_memory(buf, (uint32_t)n, 0, (uint32_t)strtoul(argv[3], NULL, 16)))
		return 1;
	f = fopen(argv[2], "wb");
	fwrite(buf, 1, n, f);
	fclose(f);
	return 0;
}

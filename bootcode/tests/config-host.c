/*
 * Host test for config.c (rof #48): parse a config.txt for a board type
 * and GPIO levels and print the result. Built and run by config-test.sh.
 *   config-host FILE BOARD_TYPE [GPIO_LEVELS_0_31]
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include <stdlib.h>

int main(int argc, char **argv) {
	static char txt[1 << 16];
	if (argc < 3)
		return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	uint32_t n = (uint32_t)fread(txt, 1, sizeof txt, f);
	fclose(f);
	struct boot_cfg c;
	cfg_defaults(&c);
	cfg_parse(&c, txt, n, (uint32_t)strtoul(argv[2], NULL, 16), argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 16) : 0, 0);
	printf("RESULT kernel=%s initramfs=%s armstub=%s dt=%s%s cmdline=%s 64=%d unknown=%u templimit=%u overlays=", c.kernel,
	       c.initramfs, c.armstub, c.device_tree_set ? "set:" : "", c.device_tree, c.cmdline, c.arm_64bit,
	       c.unknown_filters, c.temp_limit);
	for (uint32_t i = 0; i < c.n_overlays; i++)
		printf("%s%s", i ? "," : "", c.overlay[i]);
	printf(" diag=%u axi=%d boarddtb=%s\n", c.diag_period, c.diag_axi, cfg_board_dtb((uint32_t)strtoul(argv[2], NULL, 16)));
	return 0;
}

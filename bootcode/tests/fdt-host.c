/*
 * Host test for dt.c (rof #48): edit a devicetree file the way boot64
 * does and write the result. Built and run by fdt-test.sh.
 *   fdt-host IN OUT OP...   with OP one of
 *     memory SIZE           /memory reg = <0 SIZE> (hex)
 *     initrd START END      /chosen linux,initrd-start/-end (hex)
 *     bootargs FILE         /chosen bootargs = the first line of FILE
 *     overlay FILE.dtbo     apply the overlay
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include <stdlib.h>
#include <string.h>

static uint32_t slurp(const char *name, uint8_t *buf, uint32_t cap) {
	FILE *f = fopen(name, "rb");
	if (!f)
		exit(2);
	uint32_t n = (uint32_t)fread(buf, 1, cap, f);
	fclose(f);
	return n;
}

int main(int argc, char **argv) {
	static uint8_t buf[1 << 20], ov[1 << 16];
	static char txt[1 << 16];
	if (argc < 3)
		return 2;
	struct dt t;
	if (!dt_open(&t, buf, slurp(argv[1], buf, 1 << 18), sizeof buf))
		return 1;
	for (int i = 3; i < argc; i++) {
		const char *op = argv[i];
		bool ok;
		if (!strcmp(op, "memory") && i + 1 < argc) {
			ok = dt_set_memory(&t, 0, (uint32_t)strtoul(argv[++i], NULL, 16));
		} else if (!strcmp(op, "initrd") && i + 2 < argc) {
			ok = dt_set_chosen_u32(&t, "linux,initrd-start", (uint32_t)strtoul(argv[i + 1], NULL, 16)) &&
			     dt_set_chosen_u32(&t, "linux,initrd-end", (uint32_t)strtoul(argv[i + 2], NULL, 16));
			i += 2;
		} else if (!strcmp(op, "bootargs") && i + 1 < argc) {
			uint32_t n = slurp(argv[++i], (uint8_t *)txt, sizeof txt);
			ok = dt_set_chosen(&t, "bootargs", txt, n);
		} else if (!strcmp(op, "overlay") && i + 1 < argc) {
			struct dt o;
			uint32_t n = slurp(argv[++i], ov, sizeof ov);
			ok = dt_open(&o, ov, n, n) && dt_apply_overlay(&t, &o, argv[i]);
		} else {
			return 2;
		}
		if (!ok)
			return 1;
	}
	dt_finish(&t);
	FILE *f = fopen(argv[2], "wb");
	fwrite(buf, 1, dt_size(&t), f);
	fclose(f);
	return 0;
}

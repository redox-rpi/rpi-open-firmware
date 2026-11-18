/*
 * redox-rpi bootcode: fill in the memory size in a flattened devicetree
 * (#230 M2). Broadcom's start.elf writes the ARM's memory into
 * /memory@0 reg; the Raspberry Pi devicetrees ship it as zero. We only
 * overwrite that property in place, so the blob keeps its size.
 *
 * New code, written from the Devicetree Specification v0.4, chapter 5
 * (flattened devicetree format).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define FDT_MAGIC 0xd00dfeedu
#define FDT_BEGIN_NODE 1
#define FDT_END_NODE 2
#define FDT_PROP 3
#define FDT_NOP 4
#define FDT_END 9

static uint32_t be32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put_be32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static bool str_eq(const char *a, const char *b) {
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

/* "memory" or "memory@..." */
static bool is_memory_node(const char *n) {
	const char *m = "memory";
	while (*m && *n == *m)
		n++, m++;
	return !*m && (*n == 0 || *n == '@');
}

/*
 * Set /memory reg to <base size> (one address cell and one size cell, as
 * in the Pi 3 devicetrees). Returns false when the blob is not a
 * devicetree or has no such property of 8 bytes.
 */
bool fdt_set_memory(uint8_t *dtb, uint32_t len, uint32_t base, uint32_t size) {
	if (len < 40 || be32(dtb) != FDT_MAGIC) {
		printf("err fdt: no devicetree magic\n");
		return false;
	}
	uint32_t total = be32(dtb + 4), off_struct = be32(dtb + 8), off_strings = be32(dtb + 12);
	uint32_t size_struct = be32(dtb + 36);
	if (total > len || off_struct + size_struct > total || off_strings >= total) {
		printf("err fdt: header does not fit the %u bytes loaded\n", len);
		return false;
	}
	uint8_t *p = dtb + off_struct, *end = p + size_struct;
	int depth = 0;
	bool in_memory = false;
	while (p + 4 <= end) {
		uint32_t tok = be32(p);
		p += 4;
		if (tok == FDT_BEGIN_NODE) {
			const char *name = (const char *)p;
			while (p < end && *p)
				p++;
			p = (uint8_t *)(((uintptr_t)p + 4) & ~(uintptr_t)3); /* skip NUL, align */
			depth++;
			in_memory = depth == 2 && is_memory_node(name);
		} else if (tok == FDT_END_NODE) {
			depth--;
			in_memory = false;
		} else if (tok == FDT_PROP) {
			if (p + 8 > end)
				break;
			uint32_t plen = be32(p), nameoff = be32(p + 4);
			uint8_t *val = p + 8;
			p = (uint8_t *)(((uintptr_t)val + plen + 3) & ~(uintptr_t)3);
			if (in_memory && off_strings + nameoff < total &&
			    str_eq((const char *)dtb + off_strings + nameoff, "reg")) {
				if (plen != 8) {
					printf("err fdt: memory reg is %u bytes, expected 8\n", plen);
					return false;
				}
				put_be32(val, base);
				put_be32(val + 4, size);
				return true;
			}
		} else if (tok == FDT_NOP) {
			continue;
		} else {
			break; /* FDT_END or garbage */
		}
	}
	printf("err fdt: no /memory reg property\n");
	return false;
}

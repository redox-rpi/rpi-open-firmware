/*
 * redox-rpi bootcode: fill in the memory size in a flattened devicetree
 * (#230 M2). Broadcom's start.elf writes the ARM's memory into
 * /memory@0 reg; the Raspberry Pi devicetrees ship it as zero. For the
 * direct boot (#31) it also fills in /chosen linux,initrd-start and -end.
 * We only overwrite properties in place, so the blob keeps its size.
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

/* "chosen" exactly */
static bool is_chosen_node(const char *n) {
	return str_eq(n, "chosen");
}

/*
 * Find property prop in a node right below the root that match() accepts;
 * returns its value and length, or NULL. Prints why when not found.
 */
static uint8_t *find_prop(uint8_t *dtb, uint32_t len, bool (*match)(const char *), const char *prop,
			  uint32_t *plen_out) {
	if (len < 40 || be32(dtb) != FDT_MAGIC) {
		printf("err fdt: no devicetree magic\n");
		return NULL;
	}
	uint32_t total = be32(dtb + 4), off_struct = be32(dtb + 8), off_strings = be32(dtb + 12);
	uint32_t size_struct = be32(dtb + 36);
	if (total > len || off_struct + size_struct > total || off_strings >= total) {
		printf("err fdt: header does not fit the %u bytes loaded\n", len);
		return NULL;
	}
	uint8_t *p = dtb + off_struct, *end = p + size_struct;
	int depth = 0;
	bool in_node = false;
	while (p + 4 <= end) {
		uint32_t tok = be32(p);
		p += 4;
		if (tok == FDT_BEGIN_NODE) {
			const char *name = (const char *)p;
			while (p < end && *p)
				p++;
			p = (uint8_t *)(((uintptr_t)p + 4) & ~(uintptr_t)3); /* skip NUL, align */
			depth++;
			in_node = depth == 2 && match(name);
		} else if (tok == FDT_END_NODE) {
			depth--;
			in_node = false;
		} else if (tok == FDT_PROP) {
			if (p + 8 > end)
				break;
			uint32_t plen = be32(p), nameoff = be32(p + 4);
			uint8_t *val = p + 8;
			p = (uint8_t *)(((uintptr_t)val + plen + 3) & ~(uintptr_t)3);
			if (in_node && off_strings + nameoff < total &&
			    str_eq((const char *)dtb + off_strings + nameoff, prop)) {
				*plen_out = plen;
				return val;
			}
		} else if (tok == FDT_NOP) {
			continue;
		} else {
			break; /* FDT_END or garbage */
		}
	}
	return NULL;
}

/*
 * Set /memory reg to <base size> (one address cell and one size cell, as
 * in the Pi 3 devicetrees). Returns false when the blob is not a
 * devicetree or has no such property of 8 bytes.
 */
bool fdt_set_memory(uint8_t *dtb, uint32_t len, uint32_t base, uint32_t size) {
	uint32_t plen;
	uint8_t *val = find_prop(dtb, len, is_memory_node, "reg", &plen);
	if (!val) {
		printf("err fdt: no /memory reg property\n");
		return false;
	}
	if (plen != 8) {
		printf("err fdt: memory reg is %u bytes, expected 8\n", plen);
		return false;
	}
	put_be32(val, base);
	put_be32(val + 4, size);
	return true;
}

/* a 4- or 8-byte /chosen property set to v */
static bool set_chosen(uint8_t *dtb, uint32_t len, const char *prop, uint32_t v) {
	uint32_t plen;
	uint8_t *val = find_prop(dtb, len, is_chosen_node, prop, &plen);
	if (!val || (plen != 4 && plen != 8)) {
		printf("err fdt: no 4- or 8-byte /chosen %s (make-dtb.sh adds it)\n", prop);
		return false;
	}
	if (plen == 8) {
		put_be32(val, 0);
		val += 4;
	}
	put_be32(val, v);
	return true;
}

/*
 * Set /chosen linux,initrd-start and linux,initrd-end, as Broadcom's
 * start.elf does for "initramfs ... followkernel". The properties must be
 * there already (make-dtb.sh puts in zeros), so the blob keeps its size.
 */
bool fdt_set_initrd(uint8_t *dtb, uint32_t len, uint32_t start, uint32_t end) {
	return set_chosen(dtb, len, "linux,initrd-start", start) && set_chosen(dtb, len, "linux,initrd-end", end);
}

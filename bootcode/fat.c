/*
 * redox-rpi bootcode: read files from the SD card's first FAT partition
 * (#230 M2). FAT16 and FAT32, root directory only, long names (ASCII)
 * and 8.3 names, case-insensitive. Read-only.
 *
 * New code, written from Microsoft's FAT specification (fatgen103) and
 * the MBR layout. Uses sd.c for the sectors.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

static uint32_t sec[128]; /* one sector, word aligned */
static uint32_t fat_cached = 0xffffffffu;
static uint32_t fat_sec[128];

static struct {
	bool ok;
	bool fat32;
	uint32_t part;       /* first sector of the partition */
	uint32_t spc;        /* sectors per cluster */
	uint32_t fat;        /* first FAT sector */
	uint32_t root;       /* FAT16: first root dir sector */
	uint32_t root_secs;  /* FAT16: root dir sectors */
	uint32_t root_clus;  /* FAT32: root dir cluster */
	uint32_t data;       /* first data sector (cluster 2) */
} v;

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool name_eq(const char *a, const char *b) {
	while (*a && *b)
		if (lower(*a++) != lower(*b++))
			return false;
	return *a == *b;
}

static uint32_t clus_sec(uint32_t c) { return v.data + (c - 2) * v.spc; }

static uint32_t next_clus(uint32_t c) {
	uint32_t off = c * (v.fat32 ? 4 : 2);
	uint32_t s = v.fat + off / 512;
	if (s != fat_cached) {
		if (!sd_read(s, fat_sec))
			return 0;
		fat_cached = s;
	}
	const uint8_t *b = (const uint8_t *)fat_sec + off % 512;
	uint32_t n = v.fat32 ? le32(b) & 0x0fffffffu : le16(b);
	if (n < 2 || n >= (v.fat32 ? 0x0ffffff8u : 0xfff8u))
		return 0; /* end of chain (or bad) */
	return n;
}

bool fat_mount(void) {
	v.ok = false;
	fat_cached = 0xffffffffu;
	if (!g_sd.ready && !sd_init())
		return false;
	if (!sd_read(0, sec))
		return false;
	const uint8_t *b = (const uint8_t *)sec;
	if (b[510] != 0x55 || b[511] != 0xaa) {
		printf("err fat: no MBR signature\n");
		return false;
	}
	const uint8_t *pe = b + 446; /* first partition entry */
	uint8_t type = pe[4];
	v.part = le32(pe + 8);
	if (!sd_read(v.part, sec))
		return false;
	uint32_t bps = le16(b + 11), rsv = le16(b + 14), nfat = b[16], rootent = le16(b + 17);
	uint32_t fatsz = le16(b + 22) ? le16(b + 22) : le32(b + 36);
	v.spc = b[13];
	if (bps != 512 || !v.spc || !nfat) {
		printf("err fat: partition type %02X is not FAT with 512-byte sectors\n", type);
		return false;
	}
	v.fat = v.part + rsv;
	v.root = v.fat + nfat * fatsz;
	v.root_secs = (rootent * 32 + 511) / 512;
	v.data = v.root + v.root_secs;
	v.fat32 = rootent == 0;
	v.root_clus = v.fat32 ? le32(b + 44) : 0;
	v.ok = true;
	printf("fat: partition type %02X at sector %u, FAT%u, %u sectors per cluster\n",
	       type, v.part, v.fat32 ? 32 : 16, v.spc);
	return true;
}

/*
 * Walk the root directory. For each file call fn(name, first cluster,
 * size, ctx); stop when it returns true. Long names are put together
 * from their entries (ASCII part only); otherwise the 8.3 name is used.
 */
typedef bool (*dir_fn)(const char *name, uint32_t clus, uint32_t size, void *ctx);

static bool walk_root(dir_fn fn, void *ctx) {
	char lfn[256];
	bool have_lfn = false;
	uint32_t clus = v.root_clus, idx = 0;
	for (;;) {
		uint32_t s;
		if (v.fat32) {
			if (idx == v.spc) {
				clus = next_clus(clus);
				if (!clus)
					return false;
				idx = 0;
			}
			s = clus_sec(clus) + idx;
		} else {
			if (idx == v.root_secs)
				return false;
			s = v.root + idx;
		}
		idx++;
		if (!sd_read(s, sec))
			return false;
		for (int e = 0; e < 16; e++) {
			const uint8_t *d = (const uint8_t *)sec + e * 32;
			if (d[0] == 0)
				return false; /* end of directory */
			if (d[0] == 0xe5) {
				have_lfn = false;
				continue;
			}
			if (d[11] == 0x0f) { /* long name part */
				uint32_t seq = (d[0] & 0x1f);
				if (seq == 0 || seq > 20)
					continue;
				if (d[0] & 0x40) {
					for (int i = 0; i < 256; i++)
						lfn[i] = 0;
					have_lfn = true;
				}
				static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
				for (int i = 0; i < 13; i++) {
					uint32_t ch = le16(d + pos[i]);
					uint32_t at = (seq - 1) * 13 + i;
					if (at < 255 && ch != 0xffff)
						lfn[at] = ch < 0x80 ? (char)ch : '?';
				}
				continue;
			}
			if (d[11] & 0x08) { /* volume label */
				have_lfn = false;
				continue;
			}
			char shortn[13];
			int n = 0;
			for (int i = 0; i < 8 && d[i] != ' '; i++)
				shortn[n++] = (char)d[i];
			if (d[8] != ' ') {
				shortn[n++] = '.';
				for (int i = 8; i < 11 && d[i] != ' '; i++)
					shortn[n++] = (char)d[i];
			}
			shortn[n] = 0;
			uint32_t c = le16(d + 26) | (v.fat32 ? le16(d + 20) << 16 : 0);
			bool stop = (d[11] & 0x10) ? false : fn(have_lfn ? lfn : shortn, c, le32(d + 28), ctx);
			have_lfn = false;
			if (stop)
				return true;
		}
	}
}

static bool print_entry(const char *name, uint32_t clus, uint32_t size, void *ctx) {
	(void)clus;
	(void)ctx;
	printf("%10u %s\n", size, name);
	return false;
}

bool fat_list(void) {
	if (!v.ok && !fat_mount())
		return false;
	walk_root(print_entry, NULL);
	return true;
}

struct find { const char *name; uint32_t clus, size; bool found; };

static bool find_entry(const char *name, uint32_t clus, uint32_t size, void *ctx) {
	struct find *f = ctx;
	if (!name_eq(name, f->name))
		return false;
	f->clus = clus;
	f->size = size;
	f->found = true;
	return true;
}

/* load a root-directory file to addr; returns its size, or -1 */
int32_t fat_load(const char *name, uint32_t addr, uint32_t max) {
	if (!v.ok && !fat_mount())
		return -1;
	struct find f = {name, 0, 0, false};
	walk_root(find_entry, &f);
	if (!f.found) {
		printf("err fat: %s not found\n", name);
		return -1;
	}
	if (f.size > max) {
		printf("err fat: %s is %u bytes, more than %u\n", name, f.size, max);
		return -1;
	}
	uint8_t *dst = (uint8_t *)addr;
	uint32_t left = f.size, clus = f.clus;
	while (left) {
		if (clus < 2) {
			printf("err fat: %s: cluster chain ends early\n", name);
			return -1;
		}
		for (uint32_t i = 0; i < v.spc && left; i++) {
			if (!sd_read(clus_sec(clus) + i, sec)) {
				printf("err fat: read error in %s\n", name);
				return -1;
			}
			uint32_t n = left < 512 ? left : 512;
			const uint8_t *src = (const uint8_t *)sec;
			for (uint32_t k = 0; k < n; k++)
				*dst++ = src[k];
			left -= n;
		}
		if (left)
			clus = next_clus(clus);
	}
	return (int32_t)f.size;
}

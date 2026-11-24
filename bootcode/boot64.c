/*
 * redox-rpi bootcode: load armstub8, the kernel and the devicetree from
 * the SD card's FAT partition and start the ARM in AArch64 (#230 M2/M3).
 * Used by the monitor's boot64 command and by the release build (BOOT=1).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include "armstub8-builtin.h"

#ifndef BOOT_CONFIG
#define BOOT_CONFIG "config.txt"
#endif

uint32_t crc32(const volatile uint8_t *p, uint32_t n) {
	uint32_t crc = 0xffffffffu;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
	}
	return ~crc;
}

/*
 * boot64 [KERNEL [DTB]]: start the ARM in AArch64 from the FAT partition, as
 * Broadcom's start.elf does, reading the keys of config.txt that matter for
 * that (config.c, rof #48), so a card made for start.elf boots unchanged:
 *   armstub   armstub= or the card's armstub8-redox.bin / armstub8.bin,
 *             else the built-in armstub8, at ARM 0
 *   kernel    KERNEL, else kernel= (default kernel8.img), at ARM 0x80000
 *   dtb       DTB, else device_tree=, else redox-3bp.dtb / redox-zero2w.dtb
 *             if the card has it (overlays merged by make-dtb.sh), else
 *             Broadcom's devicetree for the board, at ARM 0x2000000; then
 *             config.txt's dtoverlay= lines applied on the VPU (dt.c),
 *             /memory set, /chosen bootargs from the first line of
 *             cmdline.txt (cmdline=)
 *   initrd    config.txt's initramfs line (else a file called initramfs),
 *             at ARM 0x2100000, and /chosen linux,initrd-start/-end, as
 *             start.elf does for "initramfs NAME followkernel" (#31)
 * DTB "-" skips the devicetree. DTB "@N" (hex size) uses the N bytes
 * already at ARM 0x2000000, sent over serial with "l c2000000 N C" (the
 * monitor passes the size as dtb_in_mem; no initramfs then). Returns only
 * on an error; on success the VPU stays in mbox_serve().
 */
#define B64_STUB 0xc0000000u
#define B64_KERNEL 0xc0080000u
#define B64_DTB 0xc2000000u
#define B64_INITRD 0xc2100000u
#define B64_ARM(bus) ((bus) - 0xc0000000u)
#define BOARD_ZERO2W 0x12u /* board type, revision bits 4..11 */
#define B64_MEM_SIZE MBOX_ARM_MEM_SIZE
#define B64_DTB_ROOM (B64_INITRD - B64_DTB) /* 1 MB for the devicetree to grow in */

/* the boot screen (#430): "loading NAME", the bar to pct */
static bool load_one(const char *name, uint32_t addr, uint32_t max, uint32_t pct) {
	bs_step(pct, "loading ", name);
	uint32_t t0 = timer_us();
	int32_t n = fat_load(name, addr, max);
	if (n < 0)
		return false;
	uint32_t ms = (timer_us() - t0) / 1000;
#if LOAD_CRC
	/* bit by bit over uncached memory: about 0.5 s for kernel8.img (#44) */
	printf("boot64: %s %u bytes at ARM %08X in %u ms, crc32 %08X\n", name, (uint32_t)n, B64_ARM(addr), ms,
	       crc32((const volatile uint8_t *)addr, (uint32_t)n));
#else
	printf("boot64: %s %u bytes at ARM %08X in %u ms\n", name, (uint32_t)n, B64_ARM(addr), ms);
#endif
	return true;
}

/* n in decimal into d (room for 11 bytes); returns the end */
static char *put_dec(char *d, uint32_t n) {
	char t[10];
	uint32_t k = 0;
	do
		t[k++] = (char)('0' + n % 10);
	while (n /= 10);
	while (k)
		*d++ = t[--k];
	*d = 0;
	return d;
}

/*
 * A large initramfs takes long on the SD card (#34): the newest boot
 * screen line counts the MB and the bar moves from 65 to 74 percent, in
 * steps of 5 percent of the file; the serial console gets a line every
 * 20 percent, so a slow boot does not look hung.
 */
static uint32_t g_load_step;

static void initrd_progress(uint32_t done, uint32_t total) {
	uint32_t part = total / 20 ? total / 20 : 1;
	uint32_t s = done / part;
	if (s == g_load_step || s > 20)
		return;
	g_load_step = s;
	char txt[32], *e = put_dec(txt, done >> 20);
	const char *of = " of ";
	while (*of)
		*e++ = *of++;
	e = put_dec(e, total >> 20);
	*e++ = ' ', *e++ = 'M', *e++ = 'B', *e = 0;
	bs_update(65 + s * 9 / 20, "loading initramfs ", txt);
	if (s % 4 == 0)
		printf("boot64: initramfs %u%%, %s\n", s * 5, txt);
}

/* a failed step: on the boot screen and on the serial line */
static void fail(const char *a, const char *b) {
	printf("err boot64: %s%s\n", a, b ? b : "");
	bs_fail(a, b);
}

/* the board type: bits 4..11 of the board revision */
static uint32_t board_type(void) {
	return (board_revision() >> 4) & 0xffu;
}

/* a file into scratch space (the initramfs's place, loaded last) */
static int32_t load_scratch(const char *name) {
	int32_t n = fat_size(name);
	if (n <= 0 || n > 0x10000)
		return -1;
	return fat_load(name, B64_INITRD, 0x10000u) == n ? n : -1;
}

/* the first line of cmdline.txt (or cmdline=) into /chosen bootargs */
static bool set_bootargs(struct dt *t, const char *name) {
	int32_t n = load_scratch(name);
	if (n < 0)
		return true; /* none: U-Boot cards have their own command line */
	const char *txt = (const char *)B64_INITRD;
	if (!dt_set_chosen(t, "bootargs", txt, (uint32_t)n))
		return false;
	uint32_t l = 0;
	while (l < (uint32_t)n && txt[l] && txt[l] != '\r' && txt[l] != '\n')
		l++;
	printf("boot64: bootargs from %s, %u bytes\n", name, l);
	return true;
}

/* config.txt's dtoverlay= lines, from overlays/NAME.dtbo */
static bool apply_overlays(struct dt *t, const struct boot_cfg *c) {
	for (uint32_t i = 0; i < c->n_overlays; i++) {
		char path[48] = "overlays/";
		uint32_t k = 9;
		for (const char *q = c->overlay[i]; *q && k < 40; q++)
			path[k++] = *q;
		for (const char *q = ".dtbo"; *q; q++)
			path[k++] = *q;
		path[k] = 0;
		int32_t n = load_scratch(path);
		struct dt o;
		if (n < 0) {
			fail("not found: ", path);
			return false;
		}
		if (!dt_open(&o, (uint8_t *)B64_INITRD, (uint32_t)n, (uint32_t)n) || !dt_apply_overlay(t, &o, c->overlay[i])) {
			fail("could not apply ", path);
			return false;
		}
	}
	return true;
}

/*
 * The initramfs for a direct boot (#31): the file of config.txt's
 * initramfs line; without one, a file called initramfs if the card has it
 * (our cards before #48). Nothing to do when there is none (U-Boot boots).
 * Loaded after the devicetree; /chosen gets its place. False on an error.
 */
static bool load_initrd(struct dt *t, const char *name) {
	if (!name[0]) {
		if (fat_size("initramfs") < 0)
			return true;
		name = "initramfs";
		printf("boot64: no initramfs line in config.txt, loading the card's initramfs\n");
	}
	bs_step(65, "loading ", name);
	uint32_t t0 = timer_us();
	g_load_step = ~0u; /* so 0 % prints too */
	g_fat_progress = initrd_progress;
	int32_t n = fat_load(name, B64_INITRD, B64_MEM_SIZE - B64_ARM(B64_INITRD));
	g_fat_progress = 0;
	if (n < 0) {
		fail("could not load ", name);
		return false;
	}
	uint32_t ms = (timer_us() - t0) / 1000;
	printf("boot64: %s %u bytes at ARM %08X in %u ms (%u KB/s)\n", name, (uint32_t)n, B64_ARM(B64_INITRD), ms,
	       ms ? (uint32_t)n / ms : 0);
#if LOAD_CRC
	printf("boot64: initramfs crc32 %08X\n", crc32((const volatile uint8_t *)B64_INITRD, (uint32_t)n));
#endif
	if (!dt_set_chosen_u32(t, "linux,initrd-start", B64_ARM(B64_INITRD)) ||
	    !dt_set_chosen_u32(t, "linux,initrd-end", B64_ARM(B64_INITRD) + (uint32_t)n)) {
		fail("no room for the initrd in the devicetree", 0);
		return false;
	}
	return true;
}

/* config.txt (or the build's CONFIG=) into c; none is fine */
static void read_config(struct boot_cfg *c) {
	cfg_defaults(c);
	int32_t n = load_scratch(BOOT_CONFIG);
	if (n < 0) {
		printf("boot64: no %s, defaults\n", BOOT_CONFIG);
		cfg_parse(c, "", 0, board_type(), 0, 0);
		return;
	}
	cfg_parse(c, (const char *)B64_INITRD, (uint32_t)n, board_type(), GP_LEV0, GP_LEV1);
	printf("boot64: %s: kernel %s, initramfs %s, armstub %s, device_tree %s, %u overlays%s\n", BOOT_CONFIG,
	       c->kernel, c->initramfs[0] ? c->initramfs : "-", c->armstub[0] ? c->armstub : "built-in",
	       c->device_tree_set ? (c->device_tree[0] ? c->device_tree : "none") : "default", c->n_overlays,
	       c->arm_64bit ? "" : ", arm_64bit=0 (only 64-bit is supported)");
	if (c->unknown_filters)
		printf("boot64: %u config.txt sections with filters we do not know, skipped\n", c->unknown_filters);
	if (c->temp_limit)
		thermal_set_limit(c->temp_limit);
#if HDMI
	/* the boot screen's title from the card (boot_title= in config.txt) */
	if (bs_config_title((const char *)B64_INITRD, (uint32_t)n))
		printf("boot64: boot screen title from %s\n", BOOT_CONFIG);
#endif
}

/* the armstub: armstub= from config.txt, else the card's armstub8-redox.bin
 * or armstub8.bin (our cards before #48), else the built-in one */
static bool load_armstub(const struct boot_cfg *c) {
	if (c->armstub[0]) {
		if (load_one(c->armstub, B64_STUB, B64_KERNEL - B64_STUB, 30))
			return true;
		fail("not found: ", c->armstub);
		return false;
	}
	static const char *const old[] = {"armstub8-redox.bin", "armstub8.bin"};
	for (uint32_t i = 0; i < 2; i++)
		if (fat_size(old[i]) > 0 && load_one(old[i], B64_STUB, B64_KERNEL - B64_STUB, 30))
			return true;
	volatile uint8_t *d = (volatile uint8_t *)B64_STUB;
	for (uint32_t i = 0; i < sizeof armstub8_builtin; i++)
		d[i] = armstub8_builtin[i];
	printf("boot64: built-in armstub8, %u bytes at ARM 00000000\n", (uint32_t)sizeof armstub8_builtin);
	return true;
}

/*
 * /chosen redox-rpi,diag-ring = <ARM address, bytes> of the display
 * sampler's ring (diag.c, #66), so a reader on Redox or Linux finds it
 * without the mailbox. The ring is above the ARM's memory, so no kernel
 * uses it as RAM.
 */
static bool add_diag_ring(struct dt *t) {
	uint32_t a = B64_ARM(diag_bus_addr()), n = diag_bytes();
	uint8_t v[8] = {a >> 24, a >> 16, a >> 8, a, n >> 24, n >> 16, n >> 8, n};
	int32_t c = dt_add_subnode(t, dt_root(t), "chosen");
	return c >= 0 && dt_setprop(t, c, "redox-rpi,diag-ring", v, sizeof v);
}

/*
 * /psci when the armstub answers PSCI ("PSCI" at 0x100, our armstub8 since
 * rof #40): Redox and Linux then stop and restart the Pi through it, and
 * our bootcode stays off after a SYSTEM_OFF. Without the node the kernel
 * has no way to stop the Pi but to stand still.
 */
static bool add_psci(struct dt *t) {
	const volatile uint8_t *m = (const volatile uint8_t *)(B64_STUB + 0x100);
	if (m[0] != 'P' || m[1] != 'S' || m[2] != 'C' || m[3] != 'I')
		return true;
	static const char compat[] = "arm,psci-1.0\0arm,psci-0.2";
	int32_t p = dt_add_subnode(t, dt_root(t), "psci");
	if (p < 0 || !dt_setprop(t, p, "compatible", compat, sizeof compat) || !dt_setprop_str(t, p, "method", "smc"))
		return false;
	printf("boot64: the armstub answers PSCI, /psci added (method smc)\n");
	return true;
}

/*
 * The devicetree into B64_DTB; *overlays false when it has them already.
 * dtb != NULL (the monitor's boot64 KERNEL DTB) wins, then device_tree=
 * from config.txt, then redox-3bp.dtb / redox-zero2w.dtb (our cards before
 * #48: Broadcom's devicetree with the overlays merged, make-dtb.sh), then
 * Broadcom's devicetree for the board. Returns the size, 0 for none, or
 * -1 on an error.
 */
static int32_t load_dtb(const char *dtb, const struct boot_cfg *c, bool *overlays) {
	*overlays = true;
	if (!dtb && c->device_tree_set) {
		if (!c->device_tree[0])
			return 0;
		dtb = c->device_tree;
	}
	if (!dtb) {
		uint32_t type = board_type();
		printf("boot64: board revision %06X\n", board_revision());
		const char *redox = type == BOARD_ZERO2W ? "redox-zero2w.dtb" : "redox-3bp.dtb";
		if (fat_size(redox) > 0) {
			dtb = redox;
			*overlays = false;
			printf("boot64: %s has its overlays already, config.txt's dtoverlay lines not applied\n", redox);
		} else {
			dtb = cfg_board_dtb(type);
		}
	}
	bs_step(60, "loading ", dtb);
	int32_t n = fat_load(dtb, B64_DTB, B64_DTB_ROOM);
	if (n < 0) {
		fail("not found: ", dtb);
		return -1;
	}
	printf("boot64: %s %u bytes at ARM %08X\n", dtb, (uint32_t)n, B64_ARM(B64_DTB));
	return n;
}

bool boot64(const char *kernel, const char *dtb, uint32_t dtb_in_mem) {
	if (!g_sdram_up) {
		printf("err boot64: SDRAM not set up\n");
		return false;
	}
	/* the Zero 2 W's BCM2710A1 is rated for 1000 MHz; at 1200 MHz, with no
	 * core voltage control, Redox crashed under load (an instruction
	 * abort at PC 0 while logging in, right after the governor's switch) */
	if (board_type() == BOARD_ZERO2W && arm_max_mhz > 1000u) {
		arm_max_mhz = 1000u;
		printf("boot64: Zero 2 W, ARM clock at most 1000 MHz\n");
	}
	static struct boot_cfg cfg;
	read_config(&cfg);
	if (!kernel)
		kernel = cfg.kernel;
	if (!load_armstub(&cfg))
		return false;
	if (!load_one(kernel, B64_KERNEL, B64_DTB - B64_KERNEL, 45)) {
		fail("not found: ", kernel);
		return false;
	}
	volatile uint32_t *stub = (volatile uint32_t *)B64_STUB;
	stub[0xfc / 4] = B64_ARM(B64_KERNEL); /* kernel_entry32 */
	stub[0xf8 / 4] = 0;                   /* dtb_ptr32 */
	if (dtb && dtb[0] == '-' && !dtb[1]) {
		printf("boot64: no devicetree\n");
	} else {
		int32_t n;
		bool overlays = false;
		if (dtb_in_mem) {
			n = (int32_t)dtb_in_mem;
			printf("boot64: devicetree in memory, %u bytes, crc32 %08X\n", dtb_in_mem,
			       crc32((const volatile uint8_t *)B64_DTB, dtb_in_mem));
		} else {
			n = load_dtb(dtb, &cfg, &overlays);
			if (n < 0)
				return false;
		}
		if (n > 0) {
			struct dt t;
			if (!dt_open(&t, (uint8_t *)B64_DTB, (uint32_t)n, B64_DTB_ROOM) ||
			    (overlays && !apply_overlays(&t, &cfg)) || !dt_set_memory(&t, 0, B64_MEM_SIZE) ||
			    !set_bootargs(&t, cfg.cmdline) || (!dtb_in_mem && !load_initrd(&t, cfg.initramfs)) ||
			    !add_psci(&t) || !add_diag_ring(&t)) {
				fail("devicetree: see the serial line", 0);
				return false;
			}
			dt_finish(&t);
			stub[0xf8 / 4] = B64_ARM(B64_DTB);
			printf("boot64: devicetree %u bytes at ARM %08X, memory 0-%08X\n", dt_size(&t), B64_ARM(B64_DTB),
			       B64_MEM_SIZE);
		} else {
			printf("boot64: device_tree= is empty, no devicetree\n");
		}
	}
	sd_off();
	if (cfg.diag_period) {
		diag_start(cfg.diag_period | (cfg.diag_axi ? DIAG_AXI : 0));
		printf("boot64: display sampler on, every %u us%s, ring at %08X\n", cfg.diag_period,
		       cfg.diag_axi ? ", AXI counters" : "", diag_bus_addr());
	}
	bs_step(75, "starting the ARM", 0);
	/* printed before the start: the ARM's first line came in the middle of
	 * it otherwise (Pi test 466: "rpi-boot-stub: devicok boot64") */
	printf("ok boot64, the serial line is the ARM's now\n");
	if (arm_start(true))
		mbox_serve();
	printf("err boot64: the ARM did not start\n");
	fail("the ARM did not start", 0);
	return false;
}

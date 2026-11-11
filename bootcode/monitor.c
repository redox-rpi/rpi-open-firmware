/*
 * redox-rpi bootcode: serial monitor for test builds (MONITOR=1).
 *
 * Lets a script on the serial line read and write registers, load code
 * into SDRAM over the UART and run it, so test stages need no card swap.
 * Never part of a release: it gives anyone on the serial pins full
 * control of the chip.
 *
 * Opening: only Enter within the wait opens it. A boot selector build
 * (CHAIN_IMAGE, see chain-image.S) chains its built-in bootcode.bin when
 * nobody opens it, on "boot", and after 300 s without input.
 *
 * Protocol (all numbers hex, "0x" optional; one reply line per command,
 * starting with "ok" or "err", then the prompt "rbc> " again):
 *   r A            read the 32-bit word at A          -> ok A V
 *   w A V          write V to A                       -> ok
 *   s A M / c A M  set / clear the bits M at A        -> ok A V
 *   d A N          dump N bytes from A, 16 per line   -> ... ok
 *   l A N [C]      load N bytes, sent as 2N hex digits (any other
 *                  character is skipped, so line breaks are fine);
 *                  with C, the CRC-32 must match      -> ok crc32 X
 *                  a missing digit ends after 2 s idle -> err timeout ...
 *                  a wrong CRC-32 with C given        -> err crc32 X want C
 *   crc A N        CRC-32 (zlib) of N bytes at A      -> ok crc32 X
 *   g A [a b c d]  call VPU code at A with r0..r3     -> ok ret X
 *   chain A N [R]  copy a bootcode.bin image (N bytes at A) to 0x80000000
 *                  and start it at 0x80000200, SDRAM left running; with
 *                  R = 1 take the SDRAM down first (experimental: Broadcom's
 *                  bootcode.bin hangs after it, Pi tests #313, #320); no return
 *   arm [M]        start the ARM cores at ARM address 0 (bus 0xc0000000,
 *                  load an armstub8 there first): M = 0 AArch32, otherwise
 *                  AArch64 (default); the monitor keeps running (arm.c)
 *   sd             set up the SD card (SDHOST)    -> ok sd NAME N MB SDHC|SDSC
 *   sdr L A N      read N sectors from L to A      -> ok crc32 X
 *   ls             list the FAT partition's root   -> lines, then ok
 *   fl NAME A      load file NAME from it to A     -> ok NAME N bytes crc32 X
 *   reset          full chip reset through the watchdog, as a reboot; no return
 *   boot           leave the monitor, go on with the normal boot (in a
 *                  boot selector build: reset, so the selector starts again
 *                  and boots without the monitor)
 * Before any err, input is dropped until the line is quiet for 0.5 s, so
 * the rest of a broken upload is not read as commands; the prompt follows
 * the err line, and the host can retry without a reset once it sees it.
 * Load into SDRAM through the uncached alias (0xc0000000 + phys), not
 * through 0x8...: other 0x8 addresses would evict this code from the L2.
 *
 * New code; register meanings from Broadcom's vpu_l1_cache_ctrl.h (BSD).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define L1_IC_FLUSH 0x2 /* L1_IC0/1_CONTROL_START_FLUSH */
#define L1_DC_FLUSH 0x6 /* L1_D_CONTROL_DC0_FLUSH | DC1_FLUSH */

#define CHAIN_BASE 0x80000000u
#define CHAIN_ENTRY 0x80000200u
#define CHAIN_MAX 0x19000u /* the trampoline lives at 0x80019000 */

#define LOAD_IDLE_US (2u * 1000 * 1000)
#define DRAIN_QUIET_US (500u * 1000)

static char line[128];

static void flush_caches(void) {
	L1_D_CONTROL |= L1_DC_FLUSH;
	while (L1_D_CONTROL & L1_DC_FLUSH) {
	}
	L1_IC0_CONTROL |= L1_IC_FLUSH;
	L1_IC1_CONTROL |= L1_IC_FLUSH;
	while ((L1_IC0_CONTROL | L1_IC1_CONTROL) & L1_IC_FLUSH) {
	}
}

/*
 * Runs from its own copy at 0x80019000 (section .tramp, see linker.ld)
 * while it overwrites the rest of us, so it calls nothing.
 *
 * After the copy it takes the SDRAM controller down again, as the Boot
 * ROM leaves it. A chained bootcode.bin expects that: Broadcom's skips
 * its SDRAM set-up when SD_CS bit 0 (RESTRT, which our set-up leaves
 * set) reads 1, and then assumes 256 MB (Pi test #269, part E). The
 * steps are the start of sdram.c's reset_with_timing(): standby, wait
 * for SDUP to clear, SDRAM clock off; then SD_CS back to the value the
 * Boot ROM left (EN and RESTRT clear; #313 step 0 hung with SD_CS = 0).
 * Nothing touches SDRAM after this; the image is in the L2. With
 * reset false (SDRAM never set up) it only copies and jumps.
 */
void chain_trampoline(const volatile uint32_t *src, uint32_t words, bool reset, uint32_t sdcs)
	__attribute__((section(".tramp"), noinline, noreturn));
void chain_trampoline(const volatile uint32_t *src, uint32_t words, bool reset, uint32_t sdcs) {
	volatile uint32_t *dst = (volatile uint32_t *)CHAIN_BASE;
	for (uint32_t i = 0; i < words; i++)
		dst[i] = src[i];

	if (!reset)
		goto go;
	SD_CS = (SD_CS & ~(SD_CS_DEL_KEEP_SET | SD_CS_DPD_SET | SD_CS_RESTRT_SET)) | SD_CS_STBY_SET;
	while (SD_CS & SD_CS_SDUP_SET) {
	}
	CM_SDCCTL |= CM_PASSWORD | CM_SDCCTL_UPDATE_SET;
	while (!(CM_SDCCTL & CM_SDCCTL_ACCPT_SET)) {
	}
	CM_SDCCTL = (CM_SDCCTL & ~(CM_SDCCTL_ENAB_SET | CM_SDCCTL_CTRL_SET)) | CM_PASSWORD;
	CM_SDCCTL = CM_PASSWORD | (CM_SDCCTL & CM_SDCCTL_UPDATE_CLR);
	while (CM_SDCCTL & CM_SDCCTL_ACCPT_SET) {
	}
	SD_CS = sdcs;
go:

	L1_D_CONTROL |= L1_DC_FLUSH;
	while (L1_D_CONTROL & L1_DC_FLUSH) {
	}
	L1_IC0_CONTROL |= L1_IC_FLUSH;
	L1_IC1_CONTROL |= L1_IC_FLUSH;
	while ((L1_IC0_CONTROL | L1_IC1_CONTROL) & L1_IC_FLUSH) {
	}
	/* through a register: a pc-relative branch from this copy would be
	 * resolved against the wrong address */
	__asm__ volatile("b %0" : : "r"(CHAIN_ENTRY));
	for (;;) {
	}
}

extern uint8_t __tramp_start[], __tramp_end[], __tramp_load[];

static uint32_t crc32(const volatile uint8_t *p, uint32_t n) {
	uint32_t crc = 0xffffffffu;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
	}
	return ~crc;
}

/* drop input until the line has been quiet for DRAIN_QUIET_US */
static void drain_input(void) {
	while (uart_getc_timeout(DRAIN_QUIET_US) >= 0) {
	}
}

static int hexval(int c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool parse_hex(const char *s, uint32_t *out) {
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	if (!*s)
		return false;
	uint32_t v = 0;
	for (; *s; s++) {
		int d = hexval(*s);
		if (d < 0)
			return false;
		v = (v << 4) | (uint32_t)d;
	}
	*out = v;
	return true;
}

/* reads one line with echo; false on the idle timeout */
static bool read_line(uint32_t timeout_us) {
	size_t n = 0;
	for (;;) {
		int c = uart_getc_timeout(timeout_us);
		if (c < 0)
			return false;
		if (c == '\r' || c == '\n') {
			if (n == 0)
				continue; /* CR LF pairs and empty lines */
			uart_putc('\n');
			line[n] = 0;
			return true;
		}
		if ((c == 8 || c == 127) && n > 0) {
			n--;
			printf("\b \b");
			continue;
		}
		if (c >= ' ' && n < sizeof(line) - 1) {
			line[n++] = (char)c;
			uart_putc((char)c);
		}
	}
}

static int strcmp_s(const char *a, const char *b) {
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

static int split(char *s, char **argv, int max) {
	int argc = 0;
	while (*s && argc < max) {
		while (*s == ' ')
			*s++ = 0;
		if (!*s)
			break;
		argv[argc++] = s;
		while (*s && *s != ' ')
			s++;
	}
	return argc;
}

static void cmd_load(uint32_t addr, uint32_t len, bool check, uint32_t want) {
	volatile uint8_t *p = (volatile uint8_t *)addr;
	printf("ok send %u bytes as hex\n", len);
	for (uint32_t i = 0; i < len; i++) {
		int hi = -1, lo = -1;
		while (hi < 0 || lo < 0) {
			int c = uart_getc_timeout(LOAD_IDLE_US);
			if (c < 0) {
				printf("err timeout after %u of %u bytes\n", i, len);
				return;
			}
			int d = hexval(c);
			if (d < 0)
				continue;
			if (hi < 0)
				hi = d;
			else
				lo = d;
		}
		p[i] = (uint8_t)((hi << 4) | lo);
	}
	uint32_t got = crc32(p, len);
	if (check && got != want) {
		drain_input();
		printf("err crc32 %08X want %08X\n", got, want);
		return;
	}
	printf("ok crc32 %08X\n", got);
}

static void cmd_dump(uint32_t addr, uint32_t len) {
	for (uint32_t off = 0; off < len; off += 16) {
		printf("%08X:", addr + off);
		for (uint32_t i = off; i < off + 16 && i < len; i++)
			printf(" %02X", *(volatile uint8_t *)(addr + i));
		printf("\n");
	}
	printf("ok\n");
}

static void cmd_chain(uint32_t addr, uint32_t len, bool reset) {
	if (len < 0x204 || len > CHAIN_MAX) {
		drain_input();
		printf("err length must be 0x204..0x%X\n", CHAIN_MAX);
		return;
	}
	reset = reset && g_sdram_up;
	printf("ok chain %u bytes, start 0x%08X, %s\n", len, CHAIN_ENTRY,
	       !g_sdram_up ? "SDRAM untouched" : reset ? "SDRAM reset" : "SDRAM running");
	/* let the last characters leave the UART before we go */
	udelay(2000);
	for (uint8_t *s = __tramp_load, *d = __tramp_start; d < __tramp_end;)
		*d++ = *s++;
	flush_caches();
	chain_trampoline((const volatile uint32_t *)addr, (len + 3) / 4, reset, g_rom_sdcs);
}

#if CHAIN_IMAGE
extern const uint8_t chain_image[], chain_image_end[];

/*
 * Boot selector (CHAIN_IMAGE): chain the bootcode.bin built into us. The
 * image lies inside our own copy in the L2, above 0x80000000, so the
 * trampoline's forward copy down to 0x80000000 never overwrites a word
 * it has yet to read.
 */
void monitor_chain_builtin(void) {
	uint32_t len = (uint32_t)(chain_image_end - chain_image);
	printf("rbc> chain built-in bootcode.bin, %u bytes, crc32 %08X\n", len, crc32(chain_image, len));
	cmd_chain((uint32_t)chain_image, len, false);
	panic("built-in bootcode.bin not chained");
}
#endif

/*
 * Full chip reset through the PM watchdog, as Linux's bcm2835_wdt restart:
 * 10 ticks (about 150 us), then a full reset. The Boot ROM starts again
 * and loads bootcode.bin from the card, as after a power cycle.
 */
/* PM_RSTC 0x7e10001c, PM_WDOG 0x7e100024 from Broadcom's headers */
#define PM_RSTC_WRCFG_MASK 0x30u
#define PM_RSTC_FULL_RESET 0x20u

void watchdog_reset(void) {
	printf("rbc> watchdog reset\n");
	udelay(2000);
	PM_WDOG = CM_PASSWORD | 10;
	PM_RSTC = CM_PASSWORD | (PM_RSTC & ~PM_RSTC_WRCFG_MASK) | PM_RSTC_FULL_RESET;
	for (;;) {
	}
}

typedef uint32_t (*vpu_fn)(uint32_t, uint32_t, uint32_t, uint32_t);

/* returns true when the normal boot should go on */
static void cmd_fl(const char *name, const char *addr_s) {
	uint32_t addr;
	if (!parse_hex(addr_s, &addr)) {
		printf("err bad number %s\n", addr_s);
		return;
	}
	int32_t n = fat_load(name, addr, 0x10000000u);
	if (n >= 0)
		printf("ok %s %u bytes crc32 %08X\n", name, (uint32_t)n, crc32((const volatile uint8_t *)addr, (uint32_t)n));
}

/*
 * boot64 [KERNEL [DTB]]: load armstub8, the kernel and the devicetree from
 * the FAT partition, as Broadcom's start.elf does, and start the ARM in
 * AArch64. ARM addresses: stub at 0, kernel at 0x80000, devicetree at
 * 0x2000000 (bus 0xc... = ARM 0x0...). DTB "-" skips the devicetree.
 * Without DTB: redox-3bp.dtb (Broadcom's devicetree with the overlays of
 * redox-rpi's config.txt already applied, from redox-rpi/make-dtb.sh),
 * else Broadcom's bcm2710-rpi-3-b-plus.dtb as it is. We don't apply
 * overlays on the VPU.
 */
#define B64_STUB 0xc0000000u
#define B64_KERNEL 0xc0080000u
#define B64_DTB 0xc2000000u
#define B64_ARM(bus) ((bus) - 0xc0000000u)
#define B64_MEM_SIZE 0x3e000000u /* 62 x 16 MB, as mapped by arm_start() */

static bool load_one(const char *name, uint32_t addr, uint32_t max) {
	int32_t n = fat_load(name, addr, max);
	if (n < 0)
		return false;
	printf("boot64: %s %u bytes at ARM %08X, crc32 %08X\n", name, (uint32_t)n, B64_ARM(addr),
	       crc32((const volatile uint8_t *)addr, (uint32_t)n));
	return true;
}

static void cmd_boot64(const char *kernel, const char *dtb) {
	if (!g_sdram_up) {
		printf("err boot64: SDRAM not set up\n");
		return;
	}
	if (!load_one("armstub8-redox.bin", B64_STUB, B64_KERNEL - B64_STUB) &&
	    !load_one("armstub8.bin", B64_STUB, B64_KERNEL - B64_STUB))
		return;
	if (!load_one(kernel, B64_KERNEL, B64_DTB - B64_KERNEL))
		return;
	volatile uint32_t *stub = (volatile uint32_t *)B64_STUB;
	stub[0xfc / 4] = B64_ARM(B64_KERNEL); /* kernel_entry32 */
	stub[0xf8 / 4] = 0;                   /* dtb_ptr32 */
	if (!dtb || dtb[0] != '-' || dtb[1]) {
		int32_t n = -1;
		if (!dtb) {
			dtb = "redox-3bp.dtb";
			n = fat_load(dtb, B64_DTB, 0x100000u);
			if (n < 0) {
				dtb = "bcm2710-rpi-3-b-plus.dtb";
				printf("boot64: no overlays applied (disable-bt, mmc): serial0 is the mini UART\n");
			}
		}
		if (n < 0)
			n = fat_load(dtb, B64_DTB, 0x100000u);
		if (n < 0)
			return;
		if (!fdt_set_memory((uint8_t *)B64_DTB, (uint32_t)n, 0, B64_MEM_SIZE))
			return;
		stub[0xf8 / 4] = B64_ARM(B64_DTB);
		printf("boot64: %s %u bytes at ARM %08X, memory 0-%08X\n", dtb, (uint32_t)n, B64_ARM(B64_DTB), B64_MEM_SIZE);
	}
	sd_off();
	if (arm_start(true))
		printf("ok boot64\n");
}

static void cmd_sd(void) {
	if (sd_init())
		printf("ok sd %s %u MB %s, SH_CDIV %X\n", g_sd.name, g_sd.mb, g_sd.high_capacity ? "SDHC" : "SDSC", SH_CDIV);
}

static bool run(int argc, char **argv) {
	uint32_t a[5] = {0};
	if (!strcmp_s(argv[0], "fl")) {
		if (argc == 3)
			cmd_fl(argv[1], argv[2]);
		else
			printf("err usage: fl NAME A\n");
		return false;
	}
	if (!strcmp_s(argv[0], "boot64")) {
		if (argc <= 3)
			cmd_boot64(argc > 1 ? argv[1] : "kernel8.img", argc > 2 ? argv[2] : NULL);
		else
			printf("err usage: boot64 [KERNEL [DTB]]\n");
		return false;
	}
	for (int i = 1; i < argc; i++) {
		if (!parse_hex(argv[i], &a[i - 1])) {
			drain_input();
			printf("err bad number %s\n", argv[i]);
			return false;
		}
	}
	const char *c = argv[0];
	int n = argc - 1;
	bool word = (c[0] == 'r' || c[0] == 'w' || c[0] == 's' || c[0] == 'c') && c[1] == 0;
	if (word && (a[0] & 3)) {
		drain_input();
		printf("err address not 4-byte aligned\n");
		return false;
	}
	volatile uint32_t *w = (volatile uint32_t *)a[0];

	if (!strcmp_s(c, "r") && n == 1) {
		printf("ok %08X %08X\n", a[0], *w);
	} else if (!strcmp_s(c, "w") && n == 2) {
		*w = a[1];
		printf("ok\n");
	} else if (!strcmp_s(c, "s") && n == 2) {
		*w |= a[1];
		printf("ok %08X %08X\n", a[0], *w);
	} else if (!strcmp_s(c, "c") && n == 2) {
		*w &= ~a[1];
		printf("ok %08X %08X\n", a[0], *w);
	} else if (!strcmp_s(c, "d") && n == 2) {
		cmd_dump(a[0], a[1]);
	} else if (!strcmp_s(c, "l") && (n == 2 || n == 3)) {
		cmd_load(a[0], a[1], n == 3, a[2]);
	} else if (!strcmp_s(c, "crc") && n == 2) {
		printf("ok crc32 %08X\n", crc32((const volatile uint8_t *)a[0], a[1]));
	} else if (!strcmp_s(c, "g") && n >= 1) {
		flush_caches();
		uint32_t ret = ((vpu_fn)a[0])(a[1], a[2], a[3], a[4]);
		printf("ok ret %08X\n", ret);
	} else if (!strcmp_s(c, "chain") && (n == 2 || n == 3)) {
		cmd_chain(a[0], a[1], n == 3 && a[2] != 0);
	} else if (!strcmp_s(c, "arm") && n <= 1) {
		arm_start(n == 0 || a[0] != 0);
	} else if (!strcmp_s(c, "sd") && n == 0) {
		cmd_sd();
	} else if (!strcmp_s(c, "sdr") && n == 3) {
		bool ok = g_sd.ready || sd_init();
		for (uint32_t i = 0; ok && i < a[2]; i++)
			ok = sd_read(a[0] + i, (uint32_t *)(a[1] + i * 512));
		if (ok)
			printf("ok crc32 %08X\n", crc32((const volatile uint8_t *)a[1], a[2] * 512));
		else
			printf("err sd read\n");
	} else if (!strcmp_s(c, "ls") && n == 0) {
		if (fat_list())
			printf("ok\n");
	} else if (!strcmp_s(c, "reset") && n == 0) {
		printf("ok reset\n");
		watchdog_reset();
	} else if (!strcmp_s(c, "boot") && n == 0) {
		printf("ok boot\n");
		return true;
	} else if ((!strcmp_s(c, "help") || !strcmp_s(c, "?")) && n == 0) {
		printf("r A | w A V | s A M | c A M | d A N | l A N [C] | crc A N | g A [r0..r3] | chain A N [R] | arm [M] | sd | sdr L A N | ls | fl NAME A | boot64 [K [DTB]] | reset | boot\n");
		printf("ok\n");
	} else {
		drain_input();
		printf("err unknown command or wrong argument count: %s\n", c);
	}
	return false;
}

/*
 * Only Enter (CR or LF) opens the monitor: other bytes, such as line noise
 * at power-on, are ignored. A boot selector build also goes on with the
 * normal boot after MONITOR_IDLE_US without a character, so a monitor
 * opened by mistake does not keep the Pi from booting.
 */
#if CHAIN_IMAGE
#define MONITOR_IDLE_US (300u * 1000 * 1000)
#else
#define MONITOR_IDLE_US 0xffffffffu
#endif

/* true when Enter came within wait_s */
bool monitor_wait(uint32_t wait_s) {
	printf("rbc> monitor: press Enter within %u s\n", wait_s);
	uint32_t t0 = timer_us(), wait_us = wait_s * 1000000u;
	for (;;) {
		uint32_t used = timer_us() - t0;
		int c = used < wait_us ? uart_getc_timeout(wait_us - used) : -1;
		if (c < 0) {
			printf("rbc> no input, normal boot\n");
			return false;
		}
		if (c == '\r' || c == '\n')
			return true;
	}
}

void monitor(uint32_t wait_s) {
	if (monitor_wait(wait_s))
		monitor_run();
}

/* returns on "boot" (and after the idle time in a boot selector build) */
void monitor_run(void) {
	printf("ok monitor\n");
	for (;;) {
		printf("rbc> ");
		if (!read_line(MONITOR_IDLE_US)) {
			if (CHAIN_IMAGE) {
				printf("\nrbc> idle %u s, normal boot\n", MONITOR_IDLE_US / 1000000u);
				return;
			}
			continue;
		}
		char *argv[6];
		int argc = split(line, argv, 6);
		if (argc && run(argc, argv))
			return;
	}
}

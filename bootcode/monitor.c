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
 *                  AArch64 (default); the monitor keeps running and
 *                  answers the ARM's mailbox requests while it waits for
 *                  input (arm.c, mbox.c)
 *   sd             set up the SD card (SDHOST)    -> ok sd NAME N MB SDHC|SDSC
 *   sdr L A N      read N sectors from L to A      -> ok crc32 X
 *   ls             list the FAT partition's root   -> lines, then ok
 *   fl NAME A      load file NAME from it to A     -> ok NAME N bytes crc32 X
 *   i2cscan        addresses answering on GPIO 46/47, both pin orders (i2c.c)
 *                  -> ok i2cscan N SCL s SDA d
 *   i2cr A R N     read N bytes from register R of I2C device A -> ok
 *   i2cw A R V     write V to register R of I2C device A -> ok i2cw
 *   exp            GPIO expander: LAN_RUN and power LED on -> ok exp
 *   usbon          power up USB as boot64 does (clocks, expander) -> ok usbon
 *   usb            DWC2 host: reset the root port, print the device there
 *                  and, for a hub, the high-speed devices behind it (after
 *                  usbon; network boot N1/N2, #116, #117) -> ok usb
 *   hdmi [RGB]     1920x1080 60 Hz DVI on HDMI, one colour 0xRRGGBB from the
 *                  HVS background (default ff8000, orange) (hdmi.c, #442)
 *                  -> step lines, then ok hdmi or err hdmi
 *   fb W H D [O]   ask the mailbox for a W x H framebuffer of depth D (10 or
 *                  20 for 16 or 32 bit), pixel order O (1 RGB, default; 0
 *                  BGR), as U-Boot does; HDMI shows it; fill it with colour
 *                  bars (#442 step 2) -> ok fb ... crc32 C, or err fb
 *   bs             the boot screen (bootscreen.c, #430) with the steps of a
 *                  release boot and fixed texts; boot64 then adds its steps
 *                  -> ok bs WxH 16 bit at BASE, crc32 C, HDMI start N us,
 *                  drawing M us
 *   hdmistat       frame counter, pixel valve, scheduler, plane, hotplug
 *                  -> ok hdmistat
 *   regs           clock/PLL/GPIO registers next to start.elf's -> ok regs N
 *   vpuclk D       VPU clock PLLC CORE0 / D (3..8; 4 = 250 MHz) -> ok vpuclk
 *   temp           SoC temperature (rof #108) -> ok temp N mC, throttled F, ARM K kHz
 *   armmax M       top ARM clock for the mailbox in MHz, hex (258..578;
 *                  4B0 = 1200, the default; 578 = 1400) -> ok armmax
 *   boot64 [K [D]] load armstub8, kernel K and devicetree D from the FAT
 *                  partition and start the ARM in AArch64 -> ok boot64;
 *                  then only the mailbox is served (mbox.c), no return
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
#include "usb.h"

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

/* .tramp is linked where it is stored; it is position independent and is
 * copied to TRAMP_RUN before use (__start_/__stop_ as GNU ld and TCC name
 * them) */
#define TRAMP_RUN 0x80019000u
extern uint8_t __start_tramp[], __stop_tramp[];

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
	for (uint8_t *s = __start_tramp, *d = (uint8_t *)TRAMP_RUN; s < __stop_tramp;)
		*d++ = *s++;
	flush_caches();
	void (*run)(const volatile uint32_t *, uint32_t, bool, uint32_t) =
		(void *)(TRAMP_RUN + ((uintptr_t)chain_trampoline - (uintptr_t)__start_tramp));
	run((const volatile uint32_t *)addr, (len + 3) / 4, reset, g_rom_sdcs);
	for (;;) {
	}
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

/* fb W H D [O]: the framebuffer path of #442 step 2, without the ARM */
static void cmd_fb(uint32_t w, uint32_t h, uint32_t depth, uint32_t order) {
	uint32_t base, pitch;
	if (!mbox_fb_test(w, h, depth, order, &base, &pitch)) {
		printf("err fb: the mailbox did not take %ux%u at %u bit\n", w, h, depth);
		return;
	}
	if (depth != 16 && depth != 32) {
		printf("err fb: %ux%u at %u bit allocated at %08X, but the pattern needs 16 or 32 bit\n", w, h, depth,
		       base);
		return;
	}
	volatile uint8_t *fb = (volatile uint8_t *)(ALIAS_UNCACHED | (base & 0x3fffffffu));
	uint32_t bad = fb_pattern(fb, w, h, depth, order & 1, pitch);
	printf("ok fb %ux%u %u bit %s at %08X pitch %u, pattern readback %s, crc32 %08X\n", w, h, depth,
	       order & 1 ? "RGB" : "BGR", base, pitch, bad ? "BAD" : "ok", crc32(fb, pitch * h));
}

/* boot64 [KERNEL [DTB]]: see boot64.c; DTB "@N" (hex size) uses the N
 * bytes already at ARM 0x2000000, sent with "l c2000000 N C" */
static void cmd_boot64(const char *kernel, const char *dtb) {
	uint32_t mem = 0;
	if (dtb && dtb[0] == '@') {
		if (!parse_hex(dtb + 1, &mem) || !mem || mem > 0x100000u) {
			printf("err boot64: bad size %s\n", dtb);
			return;
		}
		dtb = NULL;
	}
	boot64(kernel, dtb, mem);
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
			cmd_boot64(argc > 1 ? argv[1] : NULL, argc > 2 ? argv[2] : NULL);
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
	} else if (!strcmp_s(c, "i2cscan") && n == 0) {
		i2c_scan();
	} else if (!strcmp_s(c, "i2cr") && n == 3) {
		i2c_read(a[0], a[1], a[2]);
	} else if (!strcmp_s(c, "i2cw") && n == 3) {
		i2c_write(a[0], a[1], a[2]);
	} else if (!strcmp_s(c, "armmax") && n == 1 && a[0] >= 600 && a[0] <= 1400) {
		arm_max_mhz = a[0];
		printf("ok armmax %u MHz\n", arm_max_mhz);
	} else if (!strcmp_s(c, "temp") && n == 0) {
		int32_t mc = thermal_mc();
		printf("ok temp %d mC, throttled %08X, ARM %u kHz\n", (int)mc, thermal_flags(), arm_khz());
	} else if (!strcmp_s(c, "vpuclk") && n == 1 && a[0] >= 3 && a[0] <= 8) {
		vpu_clock(a[0]);
		printf("ok vpuclk PLLC CORE0 / %u, CM_VPUCTL %08X DIV %08X\n", a[0], CM_VPUCTL, CM_VPUDIV);
	} else if (!strcmp_s(c, "hdmi") && n <= 1) {
		if (hdmi_on(n ? a[0] : 0xff8000u))
			printf("ok hdmi\n");
		else
			printf("err hdmi: see the lines above\n");
	} else if (!strcmp_s(c, "fb") && n >= 3 && n <= 4) {
		cmd_fb(a[0], a[1], a[2], n == 4 ? a[3] : 1);
	} else if (!strcmp_s(c, "bs") && n == 0) {
		if (bs_demo())
			bs_report();
		else
			printf("err bs: see the lines above\n");
	} else if (!strcmp_s(c, "hdmistat") && n == 0) {
		hdmi_status();
		printf("ok hdmistat\n");
	} else if (!strcmp_s(c, "regs") && n == 0) {
		regs_compare();
	} else if (!strcmp_s(c, "usb") && n == 0) {
		if (!g_sdram_up)
			printf("err usb: SDRAM not set up\n");
		else if (usb_probe_root())
			printf("ok usb\n");
	} else if (!strcmp_s(c, "usbon") && n == 0) {
		if (usb_power_on())
			printf("ok usbon\n");
	} else if (!strcmp_s(c, "exp") && n == 0) {
		if (expander_start())
			printf("ok exp\n");
	} else if (!strcmp_s(c, "reset") && n == 0) {
		printf("ok reset\n");
		watchdog_reset();
	} else if (!strcmp_s(c, "boot") && n == 0) {
		printf("ok boot\n");
		return true;
	} else if (!strcmp_s(c, "diag") && n == 1) {
		diag_start(a[0]);
		printf("ok diag %s, ring at %08X, %u bytes\n", a[0] ? "on" : "off", diag_bus_addr(), diag_bytes());
	} else if (!strcmp_s(c, "diagdump") && n == 1) {
		diag_dump(a[0]);
	} else if ((!strcmp_s(c, "help") || !strcmp_s(c, "?")) && n == 0) {
		printf("r A | w A V | s A M | c A M | d A N | l A N [C] | crc A N | g A [r0..r3] | chain A N [R] | arm [M] | sd | sdr L A N | ls | fl NAME A | i2cscan | i2cr A R N | i2cw A R V | exp | usbon | usb | hdmi [RGB] | hdmistat | diag US(hex, 0 off) | diagdump N | bs | regs | vpuclk D | armmax M | temp | boot64 [K [DTB]] | reset | boot\n");
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

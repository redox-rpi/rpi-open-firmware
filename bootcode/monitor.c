/*
 * redox-rpi bootcode: serial monitor for test builds (MONITOR=1).
 *
 * Lets a script on the serial line read and write registers, load code
 * into SDRAM over the UART and run it, so test stages need no card swap.
 * Never part of a release: it gives anyone on the serial pins full
 * control of the chip.
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
 *   chain A N      copy a bootcode.bin image (N bytes at A) to 0x80000000,
 *                  take the SDRAM down as the Boot ROM leaves it, and
 *                  start the image at 0x80000200; no return
 *   boot           leave the monitor, go on with the normal boot
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
 * for SDUP to clear, SDRAM clock off; then SD_CS = 0 (EN and RESTRT
 * clear). Nothing touches SDRAM after this; the image is in the L2.
 */
void chain_trampoline(const volatile uint32_t *src, uint32_t words)
	__attribute__((section(".tramp"), noinline, noreturn));
void chain_trampoline(const volatile uint32_t *src, uint32_t words) {
	volatile uint32_t *dst = (volatile uint32_t *)CHAIN_BASE;
	for (uint32_t i = 0; i < words; i++)
		dst[i] = src[i];

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
	SD_CS = 0;

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

static void cmd_chain(uint32_t addr, uint32_t len) {
	if (len < 0x204 || len > CHAIN_MAX) {
		drain_input();
		printf("err length must be 0x204..0x%X\n", CHAIN_MAX);
		return;
	}
	printf("ok chain %u bytes, start 0x%08X, SDRAM reset\n", len, CHAIN_ENTRY);
	/* let the last characters leave the UART before we go */
	udelay(2000);
	for (uint8_t *s = __tramp_load, *d = __tramp_start; d < __tramp_end;)
		*d++ = *s++;
	flush_caches();
	chain_trampoline((const volatile uint32_t *)addr, (len + 3) / 4);
}

typedef uint32_t (*vpu_fn)(uint32_t, uint32_t, uint32_t, uint32_t);

/* returns true when the normal boot should go on */
static bool run(int argc, char **argv) {
	uint32_t a[5] = {0};
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
	} else if (!strcmp_s(c, "chain") && n == 2) {
		cmd_chain(a[0], a[1]);
	} else if (!strcmp_s(c, "boot") && n == 0) {
		printf("ok boot\n");
		return true;
	} else if ((!strcmp_s(c, "help") || !strcmp_s(c, "?")) && n == 0) {
		printf("r A | w A V | s A M | c A M | d A N | l A N [C] | crc A N | g A [r0..r3] | chain A N | boot\n");
		printf("ok\n");
	} else {
		drain_input();
		printf("err unknown command or wrong argument count: %s\n", c);
	}
	return false;
}

void monitor(uint32_t wait_s) {
	printf("rbc> monitor: press Enter within %u s\n", wait_s);
	int c = uart_getc_timeout(wait_s * 1000000u);
	if (c < 0) {
		printf("rbc> no input, normal boot\n");
		return;
	}
	printf("ok monitor\n");
	for (;;) {
		printf("rbc> ");
		if (!read_line(0xffffffffu))
			continue;
		char *argv[6];
		int argc = split(line, argv, 6);
		if (argc && run(argc, argv))
			return;
	}
}

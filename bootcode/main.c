/*
 * redox-rpi bootcode: stage 1 main, milestone M1.
 *
 * M1: clocks, UART, SDRAM and a memory test, then a heartbeat. No loading
 * yet. Test builds (MONITOR=1) open the serial monitor before the
 * heartbeat. Plan: https://github.com/redox-rpi/redox-rpi/issues/230
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

#if MONITOR && CHAIN_IMAGE
#define VARIANT "M1, monitor, test build, boot selector"
#elif MONITOR
#define VARIANT "M1, monitor, test build"
#else
#define VARIANT "M1"
#endif

uint32_t g_cpuid;
uint32_t g_rom_sdcs;
bool g_sdram_up;

extern uint32_t __bss_start[], __bss_end[];
extern const uint32_t exc_stub_table[33];

#define VECTORS ((volatile uint32_t *)0x8001b000)
#define NUM_VECTORS 128 /* 32 exceptions, then the interrupts */

static const char *const exc_name[] = {
	"zero", "misaligned", "divide by zero", "undefined instruction",
	"forbidden instruction", "illegal memory", "bus error",
	"floating point", "isp", "dummy", "icache", "vector core",
	"bad L2 alias", "breakpoint",
};

void exception_fatal(uint32_t num, const uint32_t *sp) __attribute__((noreturn));
void exception_fatal(uint32_t num, const uint32_t *sp) {
	const char *name = num < sizeof(exc_name) / sizeof(exc_name[0]) ? exc_name[num]
	                   : num == 32 ? "interrupt" : "unknown";
	/* the CPU pushed sr and pc; the stub's bl changed nothing below sp */
	printf("\nEXCEPTION %u (%s): sr 0x%08X pc 0x%08X sp %p\n", num, name, sp[0], sp[1], sp);
	for (;;) {
	}
}

void panic(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	printf("\nPANIC: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	for (;;) {
	}
}

static void vectors_init(void) {
	for (int i = 0; i < NUM_VECTORS; i++)
		VECTORS[i] = exc_stub_table[i < 32 ? i : 32];
	IC0_VADDR = (uint32_t)VECTORS;
	IC1_VADDR = (uint32_t)VECTORS;
}

/* what the Boot ROM left behind, read before we change it */
struct rom_state {
	uint32_t vpuctl, vpudiv, pllc_ctrl, pllc_frac, sdcs, timer;
};

void bootcode_main(uint32_t cpuid) __attribute__((noreturn));
void bootcode_main(uint32_t cpuid) {
	for (uint32_t *p = __bss_start; p < __bss_end; p++)
		*p = 0;
	g_cpuid = cpuid;

	struct rom_state rom = {
		CM_VPUCTL, CM_VPUDIV, A2W_PLLC_CTRL, A2W_PLLC_FRAC, SD_CS, ST_CLO_REG,
	};

	g_rom_sdcs = rom.sdcs;
	vectors_init();
	clock_init();
	uart_init();

	printf("\n\nredox-rpi bootcode " BUILD_ID " (" VARIANT ")\n");
	printf("VPU cpuid 0x%X\n", cpuid);
	printf("from the Boot ROM: CM_VPUCTL 0x%X CM_VPUDIV 0x%X PLLC_CTRL 0x%X PLLC_FRAC 0x%X SD_CS 0x%X timer %u us\n",
	       rom.vpuctl, rom.vpudiv, rom.pllc_ctrl, rom.pllc_frac, rom.sdcs, rom.timer);

#if MONITOR && CHAIN_IMAGE
	/* boot selector: the normal boot chains before our SDRAM set-up, so
	 * Broadcom's code finds the SDRAM as the Boot ROM left it (Pi test
	 * #313: after our set-up and reset it never got going) */
	if (!monitor_wait(5))
		monitor_chain_builtin();
#endif
	uint32_t t0 = timer_us();
	sdram_init();
	g_sdram_up = true;
	printf("SDRAM ready after %u us\n", timer_us() - t0);
#if MONITOR && CHAIN_IMAGE
	monitor_run();
	monitor_chain_builtin();
#elif MONITOR
	monitor(5);
#endif
	printf("M1 done: stage 1 runs, SDRAM works. Nothing to load yet.\n");

	for (uint32_t n = 1;; n++) {
		udelay(10 * 1000 * 1000);
		if (n <= 6)
			printf("alive %u s\n", n * 10);
	}
}

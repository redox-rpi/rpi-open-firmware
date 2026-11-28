/*
 * redox-rpi bootcode: stage 1 main.
 *
 * Clocks, UART and SDRAM. The release build (BOOT=1) then loads armstub8,
 * kernel8.img and the devicetree from the SD card and starts the ARM, as
 * the monitor's boot64 does. Test builds (MONITOR=1) open the serial
 * monitor instead. The plain build (M1) only shows a heartbeat. Plan: https://github.com/redox-rpi/redox-rpi/issues/230
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

/* KERNEL= in the build overrides config.txt (selector card tests) */
#ifndef BOOT_KERNEL
#define BOOT_KERNEL NULL
#endif
#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

#if MONITOR && CHAIN_IMAGE
#define VARIANT "M1, monitor, test build, boot selector"
#elif MONITOR
#define VARIANT "M1, monitor, test build"
#elif BOOT && HDMI && MBOX_TRACE
#define VARIANT "release, boots from the SD card, boot screen on HDMI, mailbox trace"
#elif BOOT && HDMI
#define VARIANT "release, boots from the SD card, boot screen on HDMI"
#elif BOOT && MBOX_TRACE
#define VARIANT "release, boots from the SD card, mailbox trace"
#elif BOOT
#define VARIANT "release, boots from the SD card"
#else
#define VARIANT "M1"
#endif

uint32_t g_cpuid;
uint32_t g_rom_sdcs;
bool g_sdram_up;

extern uint32_t __start_bss[], __stop_bss[];
extern const uint32_t exc_stub_table[128];

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
	                   : num >= 32 ? "interrupt" : "unknown";
	/* the CPU pushed sr and pc; the stub's bl changed nothing below sp */
	printf("\nEXCEPTION %u (%s): sr 0x%08X pc 0x%08X sp %p\n", num, name, sp[0], sp[1], sp);
	/* which interrupt it was (Test 706): the controllers' status and masks */
	printf("IC0 C %08X S %08X SRC %08X %08X  IC1 C %08X S %08X SRC %08X %08X\n", IC0_C, IC0_S,
	       IC0_SRC0, IC0_SRC1, IC1_C, IC1_S, IC1_SRC0, IC1_SRC1);
	printf("IC0 MASK %08X %08X %08X %08X %08X %08X %08X %08X\n", IC0_MASK0, IC0_MASK1, IC0_MASK2,
	       IC0_MASK3, IC0_MASK4, IC0_MASK5, IC0_MASK6, IC0_MASK7);
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
		VECTORS[i] = exc_stub_table[i];
	IC0_VADDR = (uint32_t)VECTORS;
	IC1_VADDR = (uint32_t)VECTORS;
}

/*
 * The OS halted the Pi (rof #40, redox-rpi #418): it wrote partition 63 to
 * PM_RSTS and reset through the watchdog, as Linux's bcm2835_wdt power-off
 * does. The partition number is spread over the even bits 0-10, so 63 is
 * 0x555. Broadcom's firmware then stays off until GPIO3 (pin 5) goes low;
 * we do the same: the ARM, USB, LAN and HDMI stay unpowered, the SDRAM is
 * not set up, the VPU runs from the 19.2 MHz oscillator with PLLC powered
 * down (the UART and the system timer run from the oscillator already, see
 * clock.c), and it only polls GPIO3 (it has the board's pull-up, as I2C
 * SDA). A low pin clears the partition and resets, so the next start
 * boots. A power cycle clears PM_RSTS too.
 */
#define PM_RSTS_PARTITION 0x555u
#define PM_RSTC_WRCFG_MASK_ 0x30u
#define PM_RSTC_FULL_RESET_ 0x20u

static void halt_if_asked(void) {
	uint32_t rsts = PM_RSTS;
	if ((rsts & PM_RSTS_PARTITION) != PM_RSTS_PARTITION)
		return;
	printf("halt: the OS halted the Pi (PM_RSTS %08X, partition 63); staying off, GPIO3 (pin 5) to ground wakes it\n",
	       rsts);
	udelay(2000); /* let the UART send the line */
	/* VPU to the oscillator as clock_init() does while it changes the
	 * divider, then PLLC off: nothing else runs from it now */
	CM_VPUCTL = CM_PASSWORD | CM_VPUCTL_FRAC_SET | CM_SRC_OSC | CM_VPUCTL_GATE_SET;
	CM_VPUDIV = CM_PASSWORD | (1u << 12);
	CM_PLLC = CM_PASSWORD | CM_PLLC_DIGRST_SET | CM_PLLC_HOLDPER_SET | CM_PLLC_HOLDCORE2_SET |
	          CM_PLLC_HOLDCORE1_SET | CM_PLLC_HOLDCORE0_SET;
	A2W_PLLC_CTRL = A2W_PASSWORD | A2W_PLLC_CTRL_PWRDN_SET;
	uint32_t low = 0;
	for (;;) {
		udelay(10000);
		low = (GP_LEV0 & (1u << 3)) ? 0 : low + 1;
		if (low >= 5) /* 50 ms low: not a glitch */
			break;
	}
	printf("halt: GPIO3 low, starting again\n");
	PM_RSTS = PM_PASSWORD | (PM_RSTS & ~PM_RSTS_PARTITION);
	udelay(2000);
	PM_WDOG = PM_PASSWORD | 10;
	PM_RSTC = PM_PASSWORD | (PM_RSTC & ~PM_RSTC_WRCFG_MASK_) | PM_RSTC_FULL_RESET_;
	for (;;) {
	}
}

/* what the Boot ROM left behind, read before we change it */
struct rom_state {
	uint32_t vpuctl, vpudiv, pllc_ctrl, pllc_frac, sdcs, timer;
};

void bootcode_main(uint32_t cpuid) __attribute__((noreturn));
void bootcode_main(uint32_t cpuid) {
	for (uint32_t *p = __start_bss; p < __stop_bss; p++)
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
	halt_if_asked();

#if MONITOR && CHAIN_IMAGE
	/* boot selector: the normal boot chains before our SDRAM set-up, so
	 * Broadcom's code finds the SDRAM as the Boot ROM left it (Pi test
	 * #313: after our set-up and reset it never got going) */
	if (!monitor_wait(5))
		monitor_chain_builtin();
#endif
	uint32_t t0 = timer_us();
	/*
	 * Chained over running SDRAM (the monitor's `chain A N`, as rbc-load.sh
	 * chainmon does): our set-up already ran and left SDUP and RESTRT set.
	 * Running it again switches the clock of a live controller and hangs
	 * (Pi test #346), so keep the SDRAM as it is. The Boot ROM leaves the
	 * controller down, and a watchdog reset takes it down too.
	 */
	const uint32_t up = SD_CS_SDUP_SET | SD_CS_RESTRT_SET;
	if ((rom.sdcs & up) == up) {
		printf("[sdram] already up (SD_CS 0x%X), kept as it is\n", rom.sdcs);
		sdram_size_kept();
	} else
		sdram_init();
	g_sdram_up = true;
	printf("SDRAM ready after %u us\n", timer_us() - t0);
#if MONITOR && CHAIN_IMAGE
	/* after our SDRAM set-up Broadcom's code does not start (Pi tests
	 * #313, #320), so leave through a chip reset: the selector starts
	 * again and, with nobody pressing Enter, chains before the set-up */
	monitor_run();
	watchdog_reset();
#elif MONITOR
	monitor(5);
#elif BOOT
	bs_start("bootcode " BUILD_ID, ", SDRAM ready", 10);
	if (sd_init()) {
		bs_step(20, "SD card ", g_sd.name);
		boot64(BOOT_KERNEL, NULL, 0);
	} else {
		bs_fail("no SD card", 0);
	}
	printf("boot failed: see the line above; nothing else to try\n");
	bs_fail("boot failed, see the serial line", 0);
#endif
#if !BOOT
	printf("M1 done: stage 1 runs, SDRAM works. Nothing to load yet.\n");
#endif

	for (uint32_t n = 1;; n++) {
		udelay(10 * 1000 * 1000);
		if (n <= 6)
			printf("alive %u s\n", n * 10);
	}
}

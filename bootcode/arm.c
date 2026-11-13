/*
 * redox-rpi bootcode: start the ARM cores (#230 M2 step 2).
 *
 * Maps the SDRAM and the peripherals into the ARM's address space, sets
 * the ARM clock from PLLB, powers the ARM domain and releases the bridge,
 * so the four Cortex-A53 cores start fetching at ARM address 0 (bus
 * 0xc0000000). With aarch64 the cores start in AArch64 at EL3, as an
 * armstub8 expects; the code at ARM 0 must be in SDRAM already.
 *
 * Sequence and values from rpi-open-firmware (Kristina Brooks, GPLv2+),
 * drivers/BCM2708ArmControl.cc (map, ARM_CONTROL0/1, PLLB, bridge with
 * the BRESP table), drivers/BCM2708PowerManagement.cc (power-up of
 * PM_PROC) and drivers/BCM2708ClockDomains.cc (PLLB DIG values),
 * upstream commit of 2019-02-04, rewritten in C. The AArch64 bit
 * (ARM_CONTROL0 bit 9, the Cortex-A53's AA64nAA32 input) is defined
 * there but never set. Register names from Broadcom's headers (BSD).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define ARM_MEMORY_BASE 0xc0000000u /* bus address of ARM 0 */
#define ARM_PERIPH_ARM 0x3f000000u  /* peripherals as the ARM sees them */
#define ARM_PERIPH_BUS 0x7e000000u
#define ARM_C0_AARCH64 0x00000200u
#define PM_CFG_CLR 0xfffcffffu

/* bridge response bits, cycled while the ARM is held, as upstream does */
static const uint8_t bresp_tab[] = {
	0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x1C, 0x18, 0x1C, 0x18, 0x0,
	0x10, 0x14, 0x10, 0x1C, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x0,
	0x10, 0x14, 0x10, 0x1C, 0x18, 0x1C, 0x10, 0x14, 0x18, 0x1C, 0x10, 0x14, 0x10, 0x0,
	0x10, 0x14, 0x18, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x0,
	0x10, 0x14, 0x18, 0x14, 0x18, 0x14, 0x10, 0x14, 0x10, 0x14, 0x10, 0x14, 0x18, 0x0,
};

static void osc_ticks(uint32_t n) {
	CM_OSCCOUNT = CM_PASSWORD | n;
	while (CM_OSCCOUNT != 0) {
	}
}

static bool pm_wait(uint32_t bit) {
	for (int i = 0; i < 20; i++) {
		if (PM_PROC & bit)
			return true;
		osc_ticks(2);
	}
	return false;
}

static void pm_write(uint32_t *pmv, uint32_t v) {
	*pmv = v;
	PM_PROC = v;
}

/* power up the ARM domain: POWUP, POWOK (trying CFG 1..3), ISPOW, MEMREP,
 * MRDONE, ISFUNC; returns false with a message on a timeout */
static bool arm_power_on(void) {
	uint32_t pmv;
	pm_write(&pmv, ((PM_PROC & PM_PROC_ARMRSTN_CLR) & PM_CFG_CLR) | PM_PASSWORD);
	udelay(15);
	pm_write(&pmv, pmv | PM_GNRIC_POWUP_SET);
	bool ok = false;
	for (uint32_t cfg = 1; cfg <= 4; cfg++) {
		if (pm_wait(PM_GNRIC_POWOK_SET)) {
			ok = true;
			break;
		}
		if (cfg == 4)
			break;
		pm_write(&pmv, (pmv & PM_CFG_CLR) | (cfg << PM_GNRIC_CFG_LSB));
	}
	if (!ok) {
		printf("err arm: no POWOK, PM_PROC %08X\n", PM_PROC);
		return false;
	}
	pm_write(&pmv, pmv | PM_GNRIC_ISPOW_SET);
	pm_write(&pmv, pmv | PM_GNRIC_MEMREP_SET);
	if (!pm_wait(PM_GNRIC_MRDONE_SET)) {
		printf("err arm: no MRDONE, PM_PROC %08X\n", PM_PROC);
		return false;
	}
	pm_write(&pmv, pmv | PM_GNRIC_ISFUNC_SET);
	return true;
}

static void arm_release_reset(void) {
	PM_PROC |= PM_PASSWORD | PM_PROC_ARMRSTN_SET;
	udelay(300);
}

/* ARM clock: PLLB = 19.2 MHz * (48 + 0xeaaa8 / 2^20), about 939 MHz,
 * ARM channel / 2, so the cores run at about 470 MHz */
static void arm_clock(void) {
	A2W_XOSC_CTRL |= A2W_PASSWORD | A2W_XOSC_CTRL_PLLBEN_SET;
	A2W_PLLB_FRAC = A2W_PASSWORD | 0xeaaa8;
	A2W_PLLB_CTRL = A2W_PASSWORD | 48 | 0x1000;
	CM_PLLB = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLLB_ANARST_SET;
	CM_PLLB = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLLB_ANARST_SET | CM_PLLB_HOLDARM_SET;
	A2W_PLLB_ANA3 = A2W_PASSWORD | 0x100;
	A2W_PLLB_ANA2 = A2W_PASSWORD | 0x0;
	A2W_PLLB_ANA1 = A2W_PASSWORD | 0x140000;
	A2W_PLLB_ANA0 = A2W_PASSWORD | 0x0;

	/* DIG values: write them back with some bits cleared, start the
	 * PLL, then write them again with the bits set (upstream setDigValues) */
	uint32_t d0 = A2W_PLLB_DIG0, d1 = A2W_PLLB_DIG1, d2 = A2W_PLLB_DIG2, d3 = A2W_PLLB_DIG3;
	CM_PLLB = CM_PASSWORD | 0xaa;
	A2W_PLLB_DIG3 = A2W_PASSWORD | d3;
	A2W_PLLB_DIG2 = A2W_PASSWORD | (d2 & 0xffeffbfe);
	A2W_PLLB_DIG1 = A2W_PASSWORD | (d1 & ~(1u << 14));
	A2W_PLLB_DIG0 = A2W_PASSWORD | d0;
	A2W_PLLB_CTRL = A2W_PASSWORD | 0x20000 | A2W_PLLB_CTRL;
	A2W_PLLB_DIG3 = A2W_PASSWORD | (d3 | 0x42);
	A2W_PLLB_DIG2 = A2W_PASSWORD | d2;
	A2W_PLLB_DIG1 = A2W_PASSWORD | d1;
	A2W_PLLB_DIG0 = A2W_PASSWORD | d0;

	A2W_PLLB_ARM = A2W_PASSWORD | 2;
	CM_PLLB = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLLB_ANARST_SET | CM_PLLB_HOLDARM_SET | CM_PLLB_LOADARM_SET;
	CM_PLLB = CM_PASSWORD | CM_PLLB_DIGRST_SET | CM_PLLB_ANARST_SET | CM_PLLB_HOLDARM_SET;
	CM_PLLB = CM_PASSWORD;
	CM_ARMCTL = CM_PASSWORD | 4 | CM_ARMCTL_ENAB_SET; /* source 4 = PLLB ARM */
}

/* one 16 MB window: ARM address arm -> bus address bus */
static void arm_map(uint32_t bus, uint32_t arm) {
	(&ARM_TRANSLATE)[arm >> 24] = bus >> 21;
}

bool arm_start(bool aarch64) {
	if (!g_sdram_up) {
		printf("err arm: SDRAM not set up\n");
		return false;
	}
	/* 0 .. 0x3e000000 to the SDRAM, 0x3f000000 to the peripherals */
	for (uint32_t i = 0; i < 62; i++)
		arm_map(ARM_MEMORY_BASE + i * 0x1000000u, i * 0x1000000u);
	arm_map(ARM_PERIPH_BUS, ARM_PERIPH_ARM);

	uint32_t id = ARM_ID;
	/*
	 * APROT: mark every ARM access as system mode. Upstream used
	 * APROTPASS (the ARM's user/system bit passed 1:1), and then user-mode
	 * (EL0) accesses to the ARM control block, e.g. the mailbox, read 0
	 * and their writes are dropped. Redox's rpi-firmwared talks to the
	 * mailbox from EL0, as it does under start.elf (Pi test rows 399, 402).
	 */
	ARM_CONTROL0 |= 0x008 | ARM_C0_APROTSYST | ARM_C0_SIZ1G | ARM_C0_FULLPERI |
	                (aarch64 ? ARM_C0_AARCH64 : 0);
	ARM_CONTROL1 |= ARM_C1_PERSON;

	arm_clock();
	if (!arm_power_on())
		return false;
	/* answer the mailbox (memory, revision, MAC, clocks) once the ARM runs */
	mbox_start();

	/* bridge: out of reset, cycle the response bits, then let it run */
	arm_release_reset();
	for (uint32_t i = 0; i < sizeof bresp_tab; i++) {
		ARM_CONTROL0 = (ARM_CONTROL0 & ~(ARM_C0_BRESP1 | ARM_C0_BRESP2)) | bresp_tab[i];
		udelay(30);
	}
	arm_release_reset();
	printf("ok arm %s, ARM_ID %08X, C0 %08X, PM_PROC %08X, entry ARM 0 = bus %08X\n",
	       aarch64 ? "aarch64" : "aarch32", id, ARM_CONTROL0, PM_PROC, ARM_MEMORY_BASE);
	udelay(2000); /* let the line leave before the ARM prints */
	ARM_CONTROL1 &= ~ARM_C1_REQSTOP;
	udelay(300);
	return true;
}

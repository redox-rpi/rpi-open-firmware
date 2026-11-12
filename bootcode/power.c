/*
 * redox-rpi bootcode: power up USB for the ARM (#230 M3 step 2).
 *
 * The Boot ROM leaves the image power domain (USB, H264, ISP) and the USB
 * domain off (Pi test row 364: PM_IMAGE 0x1000, PM_USB 0), and the ARM's
 * first DWC2 access then aborts. Broadcom's start.elf powers them before
 * the ARM starts; so do we.
 *
 * Sequence and values from rpi-open-firmware (GPLv2+), which ran on our
 * Pi 3B+ in Pi test #224: BCM2708PlatformStartup.cc (order: USB domain
 * off, image domain on, USB domain on, USB PHY),
 * drivers/BCM2708PowerManagement.cc (image domain power-up in two CFG
 * rounds, peripheral reset with the PERII clock ungated) and
 * drivers/BCM2708UsbPhy.cc (USB PHY over MDIO). Upstream's LAN reset on
 * GPIO 31 does not apply: on the 3B+ LAN_RUN is pin 3 of the FXL6408 GPIO
 * expander (i2c.c expander_start), not a SoC pin.
 * Rewritten in C.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define PM_CFG_MASK (3u << PM_GNRIC_CFG_LSB)
#define MDIO_BUSY 0x80000000u

/* start.S: the vector instructions upstream runs with the PERII clock gated */
void vpu_vector_touch(void);

static void osc_ticks(uint32_t n) {
	CM_OSCCOUNT = CM_PASSWORD | n;
	while (CM_OSCCOUNT) {
	}
}

static bool pm_wait(volatile uint32_t *reg, uint32_t bit) {
	for (int i = 0; i < 20; i++) {
		if (*reg & bit)
			return true;
		osc_ticks(2);
	}
	return false;
}

/* POWOK, trying CFG 1..3 as upstream does when it does not come */
static bool pm_powok(volatile uint32_t *reg, uint32_t *pmv) {
	for (uint32_t cfg = 1; !pm_wait(reg, PM_GNRIC_POWOK_SET); cfg++) {
		if (cfg == 4)
			return false;
		*pmv = (*pmv & ~PM_CFG_MASK) | (cfg << PM_GNRIC_CFG_LSB);
		*reg = *pmv;
	}
	return true;
}

static void pm_set(volatile uint32_t *reg, uint32_t *pmv, uint32_t bits) {
	*pmv |= bits;
	*reg = *pmv;
}

static bool image_power_on(void) {
	volatile uint32_t *reg = (volatile uint32_t *)&PM_IMAGE;
	uint32_t pmv = PM_IMAGE | (1u << PM_GNRIC_CFG_LSB) | PM_PASSWORD;
	*reg = pmv;
	pm_set(reg, &pmv, PM_GNRIC_POWUP_SET);
	if (!pm_powok(reg, &pmv))
		return false;
	/* again with CFG 3 */
	pmv &= ~PM_GNRIC_POWUP_SET;
	*reg = pmv;
	pm_set(reg, &pmv, PM_CFG_MASK);
	pm_set(reg, &pmv, PM_GNRIC_POWUP_SET);
	if (!pm_powok(reg, &pmv))
		return false;
	pm_set(reg, &pmv, PM_GNRIC_ISPOW_SET);
	pm_set(reg, &pmv, PM_GNRIC_MEMREP_SET);
	if (!pm_wait(reg, PM_GNRIC_MRDONE_SET))
		return false;
	pm_set(reg, &pmv, PM_GNRIC_ISFUNC_SET);

	/* reset the peripherals with their clock ungated */
	CM_PERIICTL = CM_PASSWORD | CM_PERIICTL_GATE_SET;
	vpu_vector_touch();
	CM_PERIICTL = CM_PASSWORD;
	osc_ticks(2);
	PM_IMAGE |= PM_PASSWORD | PM_IMAGE_PERIRSTN_SET;
	CM_PERIICTL = CM_PASSWORD | CM_PERIICTL_GATE_SET;
	return true;
}

static void mdio_wait(void) {
	for (int i = 0; i < 100000 && (USB_GMDIOCSR & MDIO_BUSY); i++) {
	}
}

static void mdio(uint32_t reg, uint32_t value, uint32_t type) {
	USB_GMDIOGEN = 0xffffffffu; /* preamble */
	mdio_wait();
	USB_GMDIOGEN = type | ((reg & 0x1f) << 18) | (value & 0xffff);
	mdio_wait();
	USB_GMDIOGEN = 0; /* dummy write, errata in the BCM2835 peripheral manual */
	mdio_wait();
}

static void phy_write(uint32_t reg, uint32_t value) { mdio(reg, value, 0x50020000u); }

static uint32_t phy_read(uint32_t reg) {
	mdio(reg, 0, 0x60020000u);
	return USB_GMDIOCSR & 0xfffff;
}

static bool usb_phy_on(void) {
	A2W_XOSC_CTRL |= A2W_PASSWORD | A2W_XOSC_CTRL_USBEN_SET;
	for (int i = 0; !(A2W_XOSC_CTRL & A2W_XOSC_CTRL_USBOK_SET); i++) {
		if (i > 100000)
			return false;
	}
	CM_TDCLKEN |= CM_PASSWORD | CM_TDCLKEN_USBDFT_SET;

	USB_GMDIOCSR = 1u << 18;
	phy_write(0x15, 272);
	phy_write(0x19, 0x4);
	phy_write(0x18, 0x10);
	phy_write(0x1d, 0x4);
	phy_write(0x17, 5682);
	for (int i = 0; phy_read(0x1b) & (1u << 7); i++) {
		if (i > 10000)
			return false;
	}
	USB_GVBUSDRV &= ~(1u << 7);
	phy_write(0x1e, 0x8000);
	phy_write(0x1d, 0x5000);
	phy_write(0x19, 0xc004);
	phy_write(0x20, 0x1c2f);
	phy_write(0x22, 0x0100);
	phy_write(0x24, 0x0010);
	phy_write(0x19, 0x0004);
	USB_GVBUSDRV = (USB_GVBUSDRV & 0xfff0ffffu) | 0xd0000;
	udelay(300);
	HW_REGISTER_RW(0x7e98100c) = 0x20402700;
	udelay(300);
	HW_REGISTER_RW(0x7e980400) = 1;
	udelay(300);
	HW_REGISTER_RW(0x7e980404) = 0xbb80;
	udelay(300);
	return true;
}

#define GPC_ENAB (1u << 4)
#define GPC_BUSY (1u << 7)

static void gp_clock(volatile uint32_t *ctl, volatile uint32_t *div, uint32_t src, uint32_t divi,
                     uint32_t mash) {
	*ctl = CM_PASSWORD | src; /* off while the divider changes */
	for (int i = 0; i < 100000 && (*ctl & GPC_BUSY); i++) {
	}
	*div = CM_PASSWORD | divi;
	*ctl = CM_PASSWORD | (mash << 9) | src;
	*ctl = CM_PASSWORD | (mash << 9) | GPC_ENAB | src;
}

/*
 * GPIO 42 GPCLK1 24 MHz for the LAN7515, GPIO 43 GPCLK2 32.768 kHz for the
 * WiFi. The 24 MHz is what start.elf sets (Pi test 337, socdump: GP1CTL
 * 0x296, GP1DIV 0x14d55, PLLD PER 500 MHz / 20.8333, MASH 1), although
 * dt-blob.dts calls it a 25 MHz output; row 373 with 25 MHz got as far as
 * "USB device not accepting new address". Ours runs from PLLC PER, also
 * 500 MHz.
 */
void eth_clock_on(void) {
	pllc_per_on();
	gp_clock((volatile uint32_t *)&CM_GP1CTL, (volatile uint32_t *)&CM_GP1DIV, 5, 0x14d55u, 1);
	/* 19.2 MHz / 585.9375, MASH 1 */
	gp_clock((volatile uint32_t *)&CM_GP2CTL, (volatile uint32_t *)&CM_GP2DIV, 1, (585u << 12) | 3840u, 1);
	/* ALT0 (GPCLK1, GPCLK2) on GPIO 42 and 43 */
	GP_FSEL4 = (GP_FSEL4 & ~((7u << 6) | (7u << 9))) | (4u << 6) | (4u << 9);
	printf("power: ETH_CLK GPIO 42 on, CM_GP1CTL %08X DIV %08X\n", CM_GP1CTL, CM_GP1DIV);
}

/*
 * Pull-ups on the SD card's CMD and DAT lines (GPIO 49-53), as start.elf
 * leaves them (Pi test 380: GPLEV1 bits 17-21 high under start.elf, low
 * under ours). U-Boot's pin control sets the function, not the pulls, and
 * its "Card did not respond to voltage select" after autoboot came and went
 * between rows 365-380.
 */
void sd_pullups(void) {
	GP_PUD = 2; /* pull up */
	udelay(5);
	GP_PUDCLK1 = 0x1fu << 17;
	udelay(5);
	GP_PUD = 0;
	GP_PUDCLK1 = 0;
}

/*
 * The VPU (core) clock: PLLC CORE0 (500 MHz) / div. clock.c starts it at
 * /4 = 125 MHz; start.elf runs it faster (Pi test 380: VPUDIV 3 from a
 * 1200 MHz CORE0). Oscillator while the divider changes, as in clock.c.
 */
void vpu_clock(uint32_t div) {
	uint32_t src = CM_VPUCTL & 0xf;
	CM_VPUCTL = CM_PASSWORD | CM_VPUCTL_FRAC_SET | CM_SRC_OSC | CM_VPUCTL_GATE_SET;
	CM_VPUDIV = CM_PASSWORD | (div << 12);
	CM_VPUCTL = CM_PASSWORD | src | CM_VPUCTL_GATE_SET;
	CM_VPUCTL = CM_PASSWORD | src | CM_VPUCTL_GATE_SET | 0x10; /* ENAB */
	sd_clock_update(); /* the SD data clock divides the core clock */
}

/*
 * The LAN7515's boot-time reset is GPIO 30 (dt-blob.dts pins_3bplus
 * LAN_RUN_BOOT, internal 30; lk-overlay arm.c pulses the same pin on the
 * 3B+ after starting the clock). GPIO 30 is an input with a pull-up from
 * power-on, so the chip leaves reset before its clock runs (rows 368-382:
 * "USB device not accepting new address"). Hold it low across the clock
 * start and LAN_RUN, then high, then an input again as start.elf leaves it
 * (Pi test 337: GPFSEL3 0, GPIO 30 high). GPIO 31 reads high under
 * start.elf and low under ours; printed for comparison.
 */
#define LAN_RUN_BOOT 30

void lan_reset_pulse_begin(void) {
	GP_CLR0 = 1u << LAN_RUN_BOOT;
	GP_FSEL3 = (GP_FSEL3 & ~7u) | 1u; /* GPIO 30 output, low */
	udelay(1000);
}

void lan_reset_pulse_end(void) {
	GP_SET0 = 1u << LAN_RUN_BOOT;
	udelay(1000);
	GP_FSEL3 = GP_FSEL3 & ~7u; /* input again */
	udelay(50000);
	printf("power: LAN reset on GPIO 30 pulsed, GPLEV0 %08X (GPIO 31 %u)\n", GP_LEV0,
	       (GP_LEV0 >> 31) & 1);
}

bool usb_power_on(void) {
	PM_USB = PM_PASSWORD; /* USB domain off while the image domain starts */
	udelay(200000);
	if (!image_power_on()) {
		printf("err power: image domain did not come up, PM_IMAGE %08X\n", PM_IMAGE);
		return false;
	}
	PM_USB |= PM_PASSWORD | PM_USB_CTRLEN_SET;
	udelay(600);
	if (!usb_phy_on()) {
		printf("err power: USB PHY did not come up\n");
		return false;
	}
	printf("power: USB on, PM_IMAGE %08X PM_USB %08X\n", PM_IMAGE, PM_USB);
	/*
	 * The LAN7515 has no crystal on the 3B+: its clock comes from GPCLK1 on
	 * GPIO 42 (dt-blob.dts pins_3bplus, ETH_CLK). Row 368 released LAN_RUN
	 * without it: U-Boot saw a device that could not send its descriptor.
	 * The clock first, then the reset.
	 */
	eth_clock_on();
	udelay(1000);
	lan_reset_pulse_begin();
	expander_start();
	lan_reset_pulse_end();
	return true;
}

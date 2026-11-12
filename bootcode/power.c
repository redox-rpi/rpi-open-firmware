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
 * GPIO 31 is left out: on the 3B+ LAN_RUN is pin 131 of the firmware's
 * GPIO expander, not a SoC pin (Raspberry Pi's dt-blob.dts, pins_3bplus).
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
	return true;
}

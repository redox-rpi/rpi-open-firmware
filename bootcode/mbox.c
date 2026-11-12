/*
 * redox-rpi bootcode: answer the ARM's mailbox property requests (#230 M3).
 *
 * After the ARM starts, U-Boot, Linux and Redox ask the firmware for the
 * memory size, the board revision, the MAC address, clock rates and power
 * states through mailbox channel 8. Broadcom's start.elf answers these;
 * here the VPU does, polled from the monitor's input loop (uart.c).
 *
 * Message format from the Raspberry Pi firmware wiki ("Mailbox property
 * interface") and U-Boot's arch/arm/mach-bcm283x/mbox.c: the ARM writes
 * the bus address of a buffer (16-byte aligned) | channel to mailbox 1;
 * we fill in the answers and write the same value back to mailbox 0.
 * Buffer: size, code (0x80000000 = done), then tags of {id, buffer size,
 * request/response length, values...}, ended by tag 0. A handled tag gets
 * bit 31 in its length word; U-Boot fails a call with any unhandled tag.
 *
 * The VPU side of the mailboxes (ARM_1_MAIL1_RD/_STA, ARM_1_MAIL0_WRT/
 * _STA) as in glennra's and librerpi's rpi-open-firmware
 * (firmware/arm_monitor.c, GPLv2+). OTP read sequence from librerpi's
 * lk-overlay (platform/bcm28xx/otp/otp.c, GPLv2): OTP row 28 is the
 * chip's serial number, row 30 the board revision (librerpi docs/otp.txt);
 * the MAC address is b8:27:eb plus the low 24 bits of the serial number.
 * Clock and power IDs from Linux's raspberrypi-firmware.h and the wiki.
 * PLL and divider formulas from Linux's clk-bcm2835.c. New code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define CHAN_PROPERTY 8
#define CODE_DONE 0x80000000u
#define TAG_RESPONSE 0x80000000u

/* where the ARM's buffer may lie: its SDRAM, 0 .. MBOX_ARM_MEM_SIZE */
#define MBOX_BUF_MAX 0x1000u

bool g_mbox_on;

/* ---- OTP ---- */

#define OTP_CMD_READ 0
#define OTP_STAT_CMD_DONE 1

static bool otp_wait(void) {
	for (int i = 0; i < 1000; i++) {
		udelay(1);
		if (OTP_STATUS_REG & OTP_STAT_CMD_DONE)
			return true;
	}
	return false;
}

static uint32_t otp_read(uint32_t row) {
	OTP_CONFIG_REG = 3;
	udelay(1);
	OTP_CTRL_HI_REG = 0;
	OTP_CTRL_LO_REG = 0;
	OTP_ADDR_REG = 0;
	OTP_DATA_REG = 0;
	OTP_CONFIG_REG = 2;

	uint32_t v = 0;
	OTP_ADDR_REG = row;
	OTP_CTRL_HI_REG = 0;
	OTP_CTRL_LO_REG = OTP_CMD_READ << 1;
	if (otp_wait()) {
		OTP_CTRL_LO_REG = (OTP_CMD_READ << 1) | 1;
		if (otp_wait())
			v = OTP_DATA_REG;
	}
	OTP_CTRL_HI_REG = 0;
	OTP_CTRL_LO_REG = 0;
	OTP_CONFIG_REG = 0;
	return v;
}

static uint32_t g_serial, g_revision;

/* ---- clocks ---- */

#define XOSC_KHZ 19200u
#define SDRAM_KHZ (19200u * 0x53 / 4) /* DDR PLL as set in sdram.c */

/* PLL output: xosc * (NDIV + FRAC / 2^20) / PDIV, in kHz */
static uint32_t pll_khz(uint32_t ctrl, uint32_t frac) {
	uint32_t ndiv = ctrl & 0x3ff, pdiv = (ctrl >> 12) & 7;
	if (!pdiv)
		pdiv = 1;
	uint32_t khz = XOSC_KHZ * ndiv + ((((frac & 0xfffff) >> 4) * XOSC_KHZ) >> 16);
	return khz / pdiv;
}

/* one PLL channel: divider in bits 0-7 (0 = 256), bit 8 disables it */
static uint32_t chan_khz(uint32_t pll, uint32_t chan) {
	if (chan & 0x100)
		return 0;
	uint32_t div = chan & 0xff;
	return pll / (div ? div : 256);
}

static uint32_t pllc_khz(void) { return pll_khz(A2W_PLLC_CTRL, A2W_PLLC_FRAC); }

/* clock manager sources of the peripheral clocks */
static uint32_t src_khz(uint32_t src) {
	switch (src) {
	case 1:
		return XOSC_KHZ;
	case 4:
		return chan_khz(pll_khz(A2W_PLLA_CTRL, A2W_PLLA_FRAC), A2W_PLLA_PER);
	case 5:
		return chan_khz(pllc_khz(), A2W_PLLC_PER);
	case 6:
		return chan_khz(pll_khz(A2W_PLLD_CTRL, A2W_PLLD_FRAC), A2W_PLLD_PER);
	}
	return 0;
}

/* a CM_xxCTL/CM_xxDIV pair: ENAB bit 4, source bits 0-3, divider 12.12 */
static uint32_t cm_hz(uint32_t ctl, uint32_t div) {
	div &= 0xffffff;
	if (!(ctl & 0x10) || div < 0x1000)
		return 0;
	return src_khz(ctl & 0xf) * 256 / (div >> 4) * 1000;
}

enum {
	CLK_EMMC = 1, CLK_UART, CLK_ARM, CLK_CORE, CLK_V3D, CLK_H264, CLK_ISP,
	CLK_SDRAM, CLK_PIXEL, CLK_PWM,
};

static uint32_t clock_hz(uint32_t id) {
	switch (id) {
	case CLK_EMMC:
		return cm_hz(CM_EMMCCTL, CM_EMMCDIV);
	case CLK_UART:
		return cm_hz(CM_UARTCTL, CM_UARTDIV);
	case CLK_PWM:
		return cm_hz(CM_PWMCTL, CM_PWMDIV);
	case CLK_ARM:
		/* arm.c: CM_ARMCTL source 4 = the PLLB ARM channel */
		return chan_khz(pll_khz(A2W_PLLB_CTRL, A2W_PLLB_FRAC), A2W_PLLB_ARM) * 1000;
	case CLK_CORE: {
		/* clock.c: the VPU (core) clock is PLLC CORE0 / CM_VPUDIV */
		uint32_t div = (CM_VPUDIV >> 12) & 0xfff;
		return div ? chan_khz(pllc_khz(), A2W_PLLC_CORE0) / div * 1000 : 0;
	}
	case CLK_SDRAM:
		return SDRAM_KHZ * 1000;
	}
	return 0;
}

/*
 * The EMMC (Arasan SDHCI) clock, which U-Boot and Redox use for the SD
 * card with dtoverlay=mmc: PLLC PER = 1000 / 2 = 500 MHz, then / 2.5 =
 * 200 MHz. clock.c leaves the PER channel held; release only that hold.
 */
/* PLLC's PER channel at PLLC / 2 (500 MHz), for the EMMC and Ethernet clocks */
void pllc_per_on(void) {
	static bool done;
	if (done)
		return;
	done = true;
	uint32_t hold = CM_PLLC & ~CM_PLLC_HOLDPER_SET & 0xffffff;
	A2W_PLLC_PER = A2W_PASSWORD | 2;
	CM_PLLC = CM_PASSWORD | hold | CM_PLLC_HOLDPER_SET | CM_PLLC_LOADPER_SET;
	CM_PLLC = CM_PASSWORD | hold | CM_PLLC_HOLDPER_SET;
	CM_PLLC = CM_PASSWORD | hold;
}

static void emmc_clock(void) {
	if (cm_hz(CM_EMMCCTL, CM_EMMCDIV))
		return;
	pllc_per_on();

	CM_EMMCCTL = CM_PASSWORD | 5; /* PLLC PER, disabled while the divider changes */
	while (CM_EMMCCTL & 0x80) {   /* BUSY */
	}
	CM_EMMCDIV = CM_PASSWORD | 0x2800; /* 2.5 */
	CM_EMMCCTL = CM_PASSWORD | (1u << 9) | 5; /* MASH 1 for the fraction */
	CM_EMMCCTL = CM_PASSWORD | (1u << 9) | 0x10 | 5;
}

/* ---- power ---- */

#define POWER_ON 1u
#define POWER_NO_DEVICE 2u
#define POWER_DEVICES 9 /* SD, UART0, UART1, USB, I2C0-2, SPI, CCP2TX */
#define POWER_USB 3

static uint32_t g_power[POWER_DEVICES];

/* ---- tags ---- */

struct tag {
	uint32_t id, size, len;
	uint32_t v[];
};

/* answer with n words (bytes if n_bytes != 0), never past the buffer */
static void reply(volatile struct tag *t, const uint32_t *v, uint32_t n_bytes) {
	uint32_t words = (n_bytes + 3) / 4, room = t->size / 4;
	for (uint32_t i = 0; i < words && i < room; i++)
		t->v[i] = v[i];
	t->len = TAG_RESPONSE | n_bytes;
}

static bool handle(volatile struct tag *t) {
	uint32_t a = t->size >= 4 ? t->v[0] : 0, b = t->size >= 8 ? t->v[1] : 0;
	uint32_t r[2];
	switch (t->id) {
	case 0x00000001: /* firmware revision */
		r[0] = 0x20261112;
		reply(t, r, 4);
		return true;
	case 0x00010001: /* board model */
		r[0] = 0;
		reply(t, r, 4);
		return true;
	case 0x00010002: /* board revision */
		r[0] = g_revision;
		reply(t, r, 4);
		return true;
	case 0x00010003: { /* MAC address, 6 bytes */
		uint8_t m[8] = {0xb8, 0x27, 0xeb, (uint8_t)(g_serial >> 16), (uint8_t)(g_serial >> 8),
		                (uint8_t)g_serial, 0, 0};
		uint32_t w[2];
		w[0] = m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 | (uint32_t)m[3] << 24;
		w[1] = m[4] | (uint32_t)m[5] << 8;
		reply(t, w, 6);
		return true;
	}
	case 0x00010004: /* board serial, 64 bits */
		r[0] = g_serial;
		r[1] = 0;
		reply(t, r, 8);
		return true;
	case 0x00010005: /* ARM memory: base, size */
		r[0] = 0;
		r[1] = MBOX_ARM_MEM_SIZE;
		reply(t, r, 8);
		return true;
	case 0x00010006: /* VideoCore memory: the rest of the 1 GB */
		r[0] = MBOX_ARM_MEM_SIZE;
		r[1] = 0x40000000u - MBOX_ARM_MEM_SIZE;
		reply(t, r, 8);
		return true;
	case 0x00010007: /* clocks list: none given */
		reply(t, r, 0);
		return true;
	case 0x00020001: /* get power state */
		r[0] = a;
		r[1] = a < POWER_DEVICES ? g_power[a] : POWER_NO_DEVICE;
		reply(t, r, 8);
		return true;
	case 0x00020002: /* power-on wait time, us */
		r[0] = a;
		r[1] = a < POWER_DEVICES ? 0 : 0xffffffffu;
		reply(t, r, 8);
		return true;
	case 0x00028001: /* set power state: bit 0 on, bit 1 wait */
		/* USB is powered in mbox_start() and never switched off: its
		 * state stays what the hardware says (power.c) */
		r[0] = a;
		if (a == POWER_USB && (b & POWER_ON) && !g_power[a] && usb_power_on())
			g_power[a] = POWER_ON;
		if (a < POWER_DEVICES) {
			if (a != POWER_USB)
				g_power[a] = b & POWER_ON;
			r[1] = g_power[a];
		} else {
			r[1] = POWER_NO_DEVICE;
		}
		reply(t, r, 8);
		return true;
	case 0x00030001: /* clock state */
		r[0] = a;
		r[1] = clock_hz(a) ? 1 : 0;
		reply(t, r, 8);
		return true;
	case 0x00030002: /* clock rate */
	case 0x00030004: /* max */
	case 0x00030007: /* min */
	case 0x00030047: /* measured */
	case 0x00038002: /* set clock rate: not changed yet, current rate */
		r[0] = a;
		r[1] = clock_hz(a);
		reply(t, r, 8);
		return true;
	case 0x00038042: { /* set SDHOST clock: rate, then two rates back */
		/* ~0 = we do not set SH_CDIV for the driver: U-Boot and Linux
		 * then program it themselves from the core clock (U-Boot
		 * bcm2835_sdhost.c, firmware_sets_cdiv; Pi test row 365) */
		uint32_t w[3] = {a, 0xffffffffu, 0xffffffffu};
		reply(t, w, 12);
		return true;
	}
	case 0x0003000a: /* max temperature, millidegrees */
		r[0] = a;
		r[1] = 85000;
		reply(t, r, 8);
		return true;
	case 0x00030046: /* throttled: never */
		r[0] = 0;
		reply(t, r, 4);
		return true;
	case 0x00030048: /* notify reboot */
		reply(t, r, 0);
		return true;
	}
	return false;
}

/* the first requests are logged on the serial line, the rest not */
#define MBOX_LOG_MAX 40
static uint32_t g_logged;

static bool in_arm_memory(uint32_t bus, uint32_t len) {
	uint32_t phys = bus & 0x3fffffffu;
	return phys < MBOX_ARM_MEM_SIZE && len <= MBOX_BUF_MAX && phys + len <= MBOX_ARM_MEM_SIZE;
}

static void property(uint32_t bus) {
	volatile uint32_t *buf = (volatile uint32_t *)(ALIAS_UNCACHED | (bus & 0x3fffffffu));
	uint32_t size = buf[0];
	if (size < 12 || (size & 3) || !in_arm_memory(bus, size)) {
		printf("mbox: bad buffer %08X size %u, dropped\n", bus, size);
		return;
	}
	bool log = g_logged < MBOX_LOG_MAX;
	if (log && ++g_logged == MBOX_LOG_MAX)
		printf("mbox: requests after this one are not logged\n");
	if (log)
		printf("mbox:");
	uint32_t i = 2;
	while (i + 3 <= size / 4 && buf[i]) {
		volatile struct tag *t = (volatile struct tag *)&buf[i];
		uint32_t tsize = t->size;
		if ((tsize & 3) || i + 3 + tsize / 4 > size / 4) {
			if (log)
				printf(" %08X (tag past the buffer)", t->id);
			break;
		}
		bool ok = handle(t);
		if (log)
			printf(" %08X%s", t->id, ok ? "" : "?");
		i += 3 + tsize / 4;
	}
	if (log)
		printf("\n");
	buf[1] = CODE_DONE;
}

/* answer one waiting request, if there is one */
void mbox_poll(void) {
	if (!g_mbox_on || (ARM_1_MAIL1_STA & ARM_MS_EMPTY))
		return;
	uint32_t msg = ARM_1_MAIL1_RD;
	if ((msg & 0xf) != CHAN_PROPERTY) {
		printf("mbox: channel %u not served, %08X dropped\n", msg & 0xf, msg);
		return;
	}
	property(msg & ~0xfu);
	while (ARM_1_MAIL0_STA & ARM_MS_FULL) {
	}
	ARM_1_MAIL0_WRT = msg;
}

/*
 * After boot64 the ARM owns the serial line: serve the mailbox and read no
 * input, so keys typed for U-Boot or Redox reach them. Never returns.
 */
void mbox_serve(void) {
	for (;;)
		mbox_poll();
}

/* start serving, right before the ARM starts */
void mbox_start(void) {
	g_serial = otp_read(28);
	g_revision = otp_read(30);
	for (int i = 0; i < POWER_DEVICES; i++)
		g_power[i] = POWER_ON;
	/* as start.elf: USB is on before the ARM starts (power.c) */
	g_power[POWER_USB] = ((PM_USB & PM_USB_CTRLEN_SET) || usb_power_on()) ? POWER_ON : 0;
	emmc_clock();
	sd_pullups();
	/* requests left from an earlier run of the ARM */
	while (!(ARM_1_MAIL1_STA & ARM_MS_EMPTY))
		(void)ARM_1_MAIL1_RD;
	g_mbox_on = true;
	printf("mbox: serving, revision %08X serial %08X, ARM %u Hz, core %u Hz, EMMC %u Hz, UART %u Hz, PM_USB %08X PM_IMAGE %08X\n",
	       g_revision, g_serial, clock_hz(CLK_ARM), clock_hz(CLK_CORE), clock_hz(CLK_EMMC), clock_hz(CLK_UART),
	       PM_USB, PM_IMAGE);
}

/*
 * redox-rpi bootcode: HDMI output, step 1 of #442 (#230 M4): a fixed
 * picture, 1920x1080 at 60 Hz in DVI mode, one colour from the HVS
 * background fill. No plane, no framebuffer yet (step 2), no EDID (step 3).
 *
 * Target values: what Broadcom's start.elf leaves on our Pi 3B+ at 1080p
 * (Pi test row 337, redox-rpi tools/socdump/reference/
 * pi3bp-firmware-1080p-display.regs). Order and bit names: Linux
 * drivers/gpu/drm/vc4 (vc4_hdmi.c, vc4_crtc.c, vc4_hvs.c),
 * drivers/clk/bcm/clk-bcm2835.c and drivers/pmdomain/bcm/bcm2835-power.c,
 * read for the sequence only; the code is new. The PLLH start follows
 * clk-bcm2835 (ANA, FRAC, CTRL, power up, release ANARST, wait for lock,
 * PRSTN). A first try in the shape of clock.c's PLLC start, which also
 * writes the DIG registers, did not lock and read back CTRL 00020001
 * (Pi test row 429).
 *
 * Every wait has a time limit, so a missing step shows up as a line on the
 * UART, not as a hang. The HDMI core is only touched after its power
 * domain, PLLH and the HSM clock are on: without them a read hangs the bus.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

/* X prefix: Broadcom's headers already use the plain register names */
#define R(a) HW_REGISTER_RW(a)

/* HVS (scaler) */
#define XHVS_DISPCTRL R(0x7e400000)
#define XHVS_DISPCTRL_EN (1u << 31)
#define XHVS_DISPLIST1 R(0x7e400024)
#define XHVS_DISPCTRL1 R(0x7e400050)
#define XHVS_DISPBKGND1 R(0x7e400054)
#define XHVS_DISPSTAT1 R(0x7e400058)
#define XHVS_DISPBASE1 R(0x7e40005c)
#define XHVS_CHAN_EN (1u << 31)
#define XHVS_CHAN_RESET (1u << 30)
#define XHVS_BKGND_FILL (1u << 24)
#define XHVS_DLIST(i) R(0x7e402000 + 4 * (i))
#define XHVS_DLIST_END 0x80000000u
#define XHVS_LIST_WORD 16 /* our display list: just the end marker */

/* pixel valve 2, which feeds HDMI from HVS channel 1 */
#define XPV2_CONTROL R(0x7e807000)
#define XPV2_V_CONTROL R(0x7e807004)
#define XPV2_HORZA R(0x7e80700c)
#define XPV2_HORZB R(0x7e807010)
#define XPV2_VERTA R(0x7e807014)
#define XPV2_VERTB R(0x7e807018)
#define XPV2_STAT R(0x7e80702c)
#define XPV_CONTROL_EN (1u << 0)
#define XPV_CONTROL_FIFO_CLR (1u << 1)
#define XPV_VCONTROL_VIDEN (1u << 0)
#define XPV_VCONTROL_CONTINUOUS (1u << 1)

/* HDMI core */
#define XHDMI_CORE_REV R(0x7e902000)
#define XHDMI_SW_RESET_CONTROL R(0x7e902004)
#define XHDMI_FIFO_CTL R(0x7e90205c)
#define XHDMI_RAM_PACKET_CONFIG R(0x7e9020a0)
#define XHDMI_SCHEDULER_CONTROL R(0x7e9020c0)
#define XHDMI_HORZA R(0x7e9020c4)
#define XHDMI_HORZB R(0x7e9020c8)
#define XHDMI_VERTA0 R(0x7e9020cc)
#define XHDMI_VERTB0 R(0x7e9020d0)
#define XHDMI_VERTA1 R(0x7e9020d4)
#define XHDMI_VERTB1 R(0x7e9020d8)
#define XHDMI_MISC_CONTROL R(0x7e9020e4)
#define XHDMI_TX_PHY_RESET_CTL R(0x7e9022c0)
#define XHDMI_TX_PHY_CTL_0 R(0x7e9022c4)
#define XSCHED_MODE_HDMI (1u << 0)
#define XSCHED_HDMI_ACTIVE (1u << 1)
#define XSCHED_IGNORE_VSYNC_PREDICTS (1u << 5)
#define XSCHED_MANUAL_FORMAT (1u << 15)
#define XFIFO_RECENTER (1u << 6)
#define XFIFO_RECENTER_DONE (1u << 14)
#define XFIFO_VALID_WRITE 0xefffu

/* HD (HDMI video side) */
#define XHD_M_CTL R(0x7e80800c)
#define XHD_VID_CTL R(0x7e808038)
#define XHD_CSC_CTL R(0x7e808040)
#define XHD_FRAME_COUNT R(0x7e808068)
#define XHD_M_SW_RST (1u << 2)
#define XHD_M_ENABLE (1u << 0)
#define XHD_VID_CTL_ENABLE (1u << 31)
#define XHD_CSC_ORDER_BGR (1u << 5) /* no conversion: full range RGB, as for a PC monitor */

/* 1080p60, CEA mode 16: h 1920 fp 88 sync 44 bp 148, v 1080 fp 4 sync 5
 * bp 36, both syncs positive, 148.5 MHz. Register values as in row 337. */
#define XMODE_W 1920u
#define XMODE_H 1080u
#define XPLLH_NDIV 77u        /* 19.2 MHz * (77 + 0x58000 / 2^20) = 1485 MHz */
#define XPLLH_FRAC 0x58000u
#define XPLLH_PIX_DIV 1u      /* PLLH channels have a fixed / 10: 148.5 MHz */
#define XHSM_DIV 0x30e0u      /* PLLC PER 500 MHz / 3.055 = 163.7 MHz, as start.elf from PLLD PER */
#define XCM_ENAB (1u << 4)
#define XCM_BUSY (1u << 7)
#define XCM_MASH1 (1u << 9)
#define XCM_LOCK_H 0x1000u    /* CM_LOCK FLOCKH on the BCM2835/7 */

static bool wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t want, uint32_t us) {
	uint32_t t0 = timer_us();
	while ((*reg & mask) != want) {
		if (timer_us() - t0 > us)
			return false;
	}
	return true;
}

static bool hdmi_power(void) {
	volatile uint32_t *pm = (volatile uint32_t *)&PM_HDMI;
	uint32_t v = (*pm & 0xffffff) | PM_PASSWORD;
	*pm = v |= PM_HDMI_RSTDR_SET;
	*pm = v |= PM_HDMI_CTRLEN_SET;
	*pm = v &= ~PM_HDMI_LDOPD_SET;
	udelay(150);
	*pm = v &= ~PM_HDMI_RSTDR_SET;
	printf("hdmi: power domain on, PM_HDMI %08X\n", PM_HDMI);
	return true;
}

static bool pllh_on(void) {
	A2W_XOSC_CTRL |= A2W_PASSWORD | A2W_XOSC_CTRL_HDMIEN_SET;

	/* held in reset and powered down while the dividers are set */
	CM_PLLH = CM_PASSWORD | CM_PLLH_ANARST_SET | CM_PLLH_DIGRST_SET;
	A2W_PLLH_CTRL = A2W_PASSWORD | A2W_PLLH_CTRL_PWRDN_SET;

	/* as bcm2835_pll_set_rate: ANA3 down to ANA0, FRAC, then CTRL */
	A2W_PLLH_ANA3 = A2W_PASSWORD | 0x0;
	A2W_PLLH_ANA2 = A2W_PASSWORD | 0x0;
	A2W_PLLH_ANA1 = A2W_PASSWORD | 0xc;
	A2W_PLLH_ANA0 = A2W_PASSWORD | 0x900000;
	A2W_PLLH_FRAC = A2W_PASSWORD | XPLLH_FRAC;
	A2W_PLLH_CTRL = A2W_PASSWORD | A2W_PLLH_CTRL_PWRDN_SET | XPLLH_NDIV | (1u << 12); /* PDIV 1 */
	printf("hdmi: PLLH set, CTRL %08X FRAC %08X ANA0 %08X ANA1 %08X XOSC %08X CM_PLLH %08X\n",
	       A2W_PLLH_CTRL, A2W_PLLH_FRAC, A2W_PLLH_ANA0, A2W_PLLH_ANA1, A2W_XOSC_CTRL, CM_PLLH);

	/* as bcm2835_pll_on: power up, out of reset, wait for lock, PRSTN */
	A2W_PLLH_CTRL = A2W_PASSWORD | XPLLH_NDIV | (1u << 12);
	CM_PLLH = CM_PASSWORD | CM_PLLH_DIGRST_SET;
	CM_PLLH = CM_PASSWORD;
	bool lock = wait_set((volatile uint32_t *)&CM_LOCK, XCM_LOCK_H, XCM_LOCK_H, 10000);
	A2W_PLLH_CTRL = A2W_PASSWORD | XPLLH_NDIV | (1u << 12) | A2W_PLLH_CTRL_PRSTN_SET;

	/* the pixel channel (as bcm2835_pll_divider_set_rate and _on); AUX
	 * and RCAL stay off as under start.elf */
	A2W_PLLH_PIX = A2W_PASSWORD | XPLLH_PIX_DIV;
	CM_PLLH = CM_PASSWORD | CM_PLLH_LOADPIX_SET;
	CM_PLLH = CM_PASSWORD;

	printf("hdmi: PLLH %s, CTRL %08X FRAC %08X PIX %08X CM_PLLH %08X CM_LOCK %08X\n",
	       lock ? "locked" : "NOT locked", A2W_PLLH_CTRL, A2W_PLLH_FRAC, A2W_PLLH_PIX, CM_PLLH, CM_LOCK);
	return lock;
}

static bool hsm_on(void) {
	pllc_per_on();
	CM_HSMCTL = CM_PASSWORD | CM_SRC_PLLC_PER;
	if (!wait_set((volatile uint32_t *)&CM_HSMCTL, XCM_BUSY, 0, 10000)) {
		printf("hdmi: HSM clock stays busy, CM_HSMCTL %08X\n", CM_HSMCTL);
		return false;
	}
	CM_HSMDIV = CM_PASSWORD | XHSM_DIV;
	CM_HSMCTL = CM_PASSWORD | XCM_MASH1 | CM_SRC_PLLC_PER;
	CM_HSMCTL = CM_PASSWORD | XCM_MASH1 | XCM_ENAB | CM_SRC_PLLC_PER;
	bool on = wait_set((volatile uint32_t *)&CM_HSMCTL, XCM_BUSY, XCM_BUSY, 10000);
	printf("hdmi: HSM clock %s, CM_HSMCTL %08X DIV %08X\n", on ? "on" : "NOT running", CM_HSMCTL,
	       CM_HSMDIV);
	return on;
}

/* HVS channel 1: an empty display list over a background fill colour */
static void hvs_channel(uint32_t rgb) {
	if (!(XHVS_DISPCTRL & XHVS_DISPCTRL_EN))
		XHVS_DISPCTRL = 0x9a0c0000u; /* as start.elf, without its interrupt enables */
	XHVS_DLIST(XHVS_LIST_WORD) = XHVS_DLIST_END;
	XHVS_DISPBASE1 = 0x3c8f0800u; /* COB 2048..15503, as start.elf */
	XHVS_DISPLIST1 = XHVS_LIST_WORD;
	XHVS_DISPCTRL1 = XHVS_CHAN_RESET;
	XHVS_DISPCTRL1 = 0;
	XHVS_DISPBKGND1 = XHVS_BKGND_FILL | (rgb & 0xffffff);
	XHVS_DISPCTRL1 = XHVS_CHAN_EN | (XMODE_W << 12) | XMODE_H;
	printf("hdmi: HVS DISPCTRL %08X, channel 1 %08X background %08X\n", XHVS_DISPCTRL, XHVS_DISPCTRL1,
	       XHVS_DISPBKGND1);
}

/* a line before each step: if a register access hangs the bus, the last
 * line on the UART names it (Pi test row 432 hung after the HSM clock) */
#define STEP(what) printf("hdmi: step %s\n", what)

static void hdmi_reset_and_timing(void) {
	STEP("read HD M_CTL");
	printf("hdmi: HD M_CTL %08X\n", XHD_M_CTL);
	STEP("read HDMI CORE_REV");
	printf("hdmi: HDMI CORE_REV %08X (start.elf: 00000600)\n", XHDMI_CORE_REV);
	STEP("HD reset");
	XHD_M_CTL = XHD_M_SW_RST;
	udelay(1);
	XHD_M_CTL = 0;
	XHD_M_CTL = XHD_M_ENABLE | (1u << 9); /* 0x201, as start.elf */
	STEP("HDMI core reset");
	XHDMI_SW_RESET_CONTROL = 3;          /* HDMI and format detect */
	XHDMI_SW_RESET_CONTROL = 0;

	STEP("PHY reset");
	XHDMI_TX_PHY_RESET_CTL = 0xfu << 16;
	XHDMI_TX_PHY_RESET_CTL = 0;

	STEP("timing");
	XHDMI_SCHEDULER_CONTROL |= XSCHED_MANUAL_FORMAT | XSCHED_IGNORE_VSYNC_PREDICTS;
	XHDMI_HORZA = 0x00006780u; /* active 1920, hsync and vsync positive */
	XHDMI_HORZB = 0x0940b058u; /* bp 148, sync 44, fp 88 */
	XHDMI_VERTA0 = 0x00508438u; /* sync 5, fp 4, active 1080 */
	XHDMI_VERTB0 = 0x00000024u; /* bp 36 */
	XHDMI_VERTA1 = 0x00508438u;
	XHDMI_VERTB1 = 0x00000024u;
	XHDMI_MISC_CONTROL = 0;     /* no pixel repetition */
	STEP("HD video and CSC");
	XHD_VID_CTL = 0;            /* positive syncs, video off until the valve runs */
	XHD_CSC_CTL = XHD_CSC_ORDER_BGR;
}

static void pv2_start(void) {
	STEP("pixel valve 2");
	XPV2_CONTROL = 0;
	XPV2_CONTROL = XPV_CONTROL_FIFO_CLR | XPV_CONTROL_EN;
	XPV2_CONTROL = 0;
	XPV2_HORZA = 0x0094002cu; /* bp 148, sync 44 */
	XPV2_HORZB = 0x00580780u; /* fp 88, active 1920 */
	XPV2_VERTA = 0x00240005u; /* bp 36, sync 5 */
	XPV2_VERTB = 0x00040438u; /* fp 4, active 1080 */
	XPV2_V_CONTROL = XPV_VCONTROL_CONTINUOUS;
	XPV2_CONTROL = 0x00177005u & ~XPV_CONTROL_EN; /* format, FIFO level, HDMI clock, as start.elf */
	XPV2_CONTROL |= XPV_CONTROL_EN;
}

static bool dvi_mode(void) {
	STEP("DVI mode");
	XHDMI_RAM_PACKET_CONFIG = 0; /* no InfoFrames in DVI mode */
	XHDMI_SCHEDULER_CONTROL &= ~XSCHED_MODE_HDMI;
	bool off = wait_set((volatile uint32_t *)&XHDMI_SCHEDULER_CONTROL, XSCHED_HDMI_ACTIVE, 0, 100000);

	uint32_t drift = XHDMI_FIFO_CTL & XFIFO_VALID_WRITE;
	XHDMI_FIFO_CTL = drift & ~XFIFO_RECENTER;
	XHDMI_FIFO_CTL = drift | XFIFO_RECENTER;
	udelay(1000);
	XHDMI_FIFO_CTL = drift & ~XFIFO_RECENTER;
	XHDMI_FIFO_CTL = drift | XFIFO_RECENTER;
	bool fifo = wait_set((volatile uint32_t *)&XHDMI_FIFO_CTL, XFIFO_RECENTER_DONE, XFIFO_RECENTER_DONE, 100000);
	printf("hdmi: DVI mode %s, scheduler %08X, FIFO recenter %s (%08X)\n", off ? "set" : "NOT set",
	       XHDMI_SCHEDULER_CONTROL, fifo ? "done" : "NOT done", XHDMI_FIFO_CTL);
	return off && fifo;
}

void hdmi_status(void) {
	if (!(CM_HSMCTL & XCM_ENAB)) {
		printf("hdmi: HSM clock off, not reading the HDMI core\n");
		return;
	}
	uint32_t f0 = XHD_FRAME_COUNT, s0 = XHVS_DISPSTAT1;
	udelay(100000);
	uint32_t f1 = XHD_FRAME_COUNT, s1 = XHVS_DISPSTAT1;
	printf("hdmi: frames %u in 100 ms (count %08X), HVS DISPSTAT1 %08X -> %08X, PV2 CONTROL %08X "
	       "V_CONTROL %08X STAT %08X, VID_CTL %08X, scheduler %08X, PHY %08X %08X, hotplug (GPIO 28, active low) %s\n",
	       f1 - f0, f1, s0, s1, XPV2_CONTROL, XPV2_V_CONTROL, XPV2_STAT, XHD_VID_CTL, XHDMI_SCHEDULER_CONTROL,
	       XHDMI_TX_PHY_RESET_CTL, XHDMI_TX_PHY_CTL_0, (GP_LEV0 >> 28) & 1 ? "no monitor" : "monitor connected");
}

/* 1080p60 DVI with one colour (0xRRGGBB); true when every step answered */
bool hdmi_on(uint32_t rgb) {
	printf("hdmi: 1920x1080 60 Hz DVI, colour %06X\n", rgb & 0xffffff);
	hvs_channel(rgb);
	if (!hdmi_power() || !pllh_on() || !hsm_on())
		return false;
	hdmi_reset_and_timing();
	pv2_start();
	STEP("video on");
	XHD_VID_CTL = XHD_VID_CTL_ENABLE;
	XPV2_V_CONTROL |= XPV_VCONTROL_VIDEN;
	bool ok = dvi_mode();
	hdmi_status();
	return ok;
}

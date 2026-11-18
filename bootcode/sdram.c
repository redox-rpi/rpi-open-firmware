/*
 * redox-rpi bootcode: LPDDR2 SDRAM set-up.
 *
 * Taken from rpi-open-firmware firmware/sdram.c (commit 82a6264, GPLv2+,
 * Copyright (C) 2016-2017 Authors of rpi-open-firmware, Copyright (C) 2016
 * Julian Brown), which brought up the 1 GB Elpida LPDDR2 of our Pi 3B+ in
 * Pi test #224. The register sequence, timings and magic values are
 * unchanged. Our changes: plain C with static functions and (void)
 * prototypes, our enum and logging, the DDR rate printed in MHz, and the
 * self-test through the uncached alias at every 64 MB as well.
 * lk-overlay's copy (platform/bcm28xx/rpi-ddr2/sdram.c, 50c95c1) has the
 * same sequence.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define MR_REQUEST_SUCCESS(x) ((SD_MR_TIMEOUT_SET & (x)) != SD_MR_TIMEOUT_SET)
#define MR_GET_RDATA(x) (((x) & SD_MR_RDATA_SET) >> SD_MR_RDATA_LSB)

#define BIST_pvt 0x20
#define PVT_calibrate_request 0x1

#define logf(fmt, ...) printf("[sdram] " fmt, ##__VA_ARGS__)

enum ram_size g_ram_size = RAM_UNKNOWN;

static const char *const size_name[] = { "1 GB", "512 MB", "256 MB", "128 MB", "unknown" };

static const char *lpddr2_manufacturer_name(uint32_t mr) {
	switch (mr) {
	case 1: return "Samsung";
	case 2: return "Qimonda";
	case 3: return "Elpida";
	case 4: return "Etron";
	case 5: return "Nanya";
	case 6: return "Hynix";
	case 0xff: return "Micron";
	default: return "unknown";
	}
}

static enum ram_size lpddr2_size(uint32_t mr8) {
	switch (mr8) {
	case 0x58: return RAM_1GB;
	case 0x18: return RAM_512MB;
	case 0x14: return RAM_256MB;
	case 0x10: return RAM_128MB;
	default: return RAM_UNKNOWN;
	}
}

static void clkman_update_begin(void) {
	CM_SDCCTL |= CM_PASSWORD | CM_SDCCTL_UPDATE_SET;
	while (!(CM_SDCCTL & CM_SDCCTL_ACCPT_SET)) {
	}
}

static void clkman_update_end(void) {
	CM_SDCCTL = CM_PASSWORD | (CM_SDCCTL & CM_SDCCTL_UPDATE_CLR);
	while (CM_SDCCTL & CM_SDCCTL_ACCPT_SET) {
	}
}

static void reset_phy_dll(void) {
	/* tell the controller we are changing the address lines */
	APHY_CSR_PHY_BIST_CNTRL_SPR = 0x30;

	DPHY_CSR_GLBL_DQ_DLL_RESET = 0x1;
	APHY_CSR_GLBL_ADDR_DLL_RESET = 0x1;

	/* a few register reads as a short stall */
	(void)SD_CS;
	(void)SD_CS;
	(void)SD_CS;
	(void)SD_CS;

	DPHY_CSR_GLBL_DQ_DLL_RESET = 0x0;
	APHY_CSR_GLBL_ADDR_DLL_RESET = 0x0;

	while ((DPHY_CSR_GLBL_MSTR_DLL_LOCK_STAT & 0xffff) != 0xffff) {
	}
}

struct lpddr2_timings {
	uint32_t tRPab, tRPpb, tRCD, tWR, tRASmin, tRRD, tWTR, tXSR, tXP, tRFCab;
	uint32_t tRTP, tFAW, tRC, tREFI;
	uint32_t tINIT1, tINIT3, tINIT5;
	uint32_t rowbits, colbits, banklow;
};

/* in DDR clocks at 400 MHz; values as upstream */
static struct lpddr2_timings timings = {
	.tREFI = 3113,   /* 7.8 us refresh */
	.tRFCab = 50,
	.tRRD = 2,
	.tWR = 7,
	.tWTR = 4,
	.tRPab = 7,
	.tRC = 24,
	.tXP = 1,
	.tRASmin = 15,
	.tRPpb = 6,
	.tRCD = 6,
	.tFAW = 18,
	.tRTP = 1,
	.tXSR = 54,
	.tINIT1 = 40,
	.tINIT3 = 79800, /* 200 us */
	.tINIT5 = 3990,  /* 10 us */
	.rowbits = 2,
	.colbits = 1,
	.banklow = 2,
};

static void reset_with_timing(const struct lpddr2_timings *t) {
	uint32_t ctrl = 0x4;

	SD_CS = (SD_CS & ~(SD_CS_DEL_KEEP_SET | SD_CS_DPD_SET | SD_CS_RESTRT_SET)) | SD_CS_STBY_SET;
	while (SD_CS & SD_CS_SDUP_SET) {
	}

	/* SDRAM clock off */
	clkman_update_begin();
	CM_SDCCTL = (CM_SDCCTL & ~(CM_SDCCTL_ENAB_SET | CM_SDCCTL_CTRL_SET)) | CM_PASSWORD;
	clkman_update_end();

	/* DDR PLL in the PHY: 19.2 MHz * 0x53 / 4 = 398.4 MHz */
	APHY_CSR_DDR_PLL_PWRDWN = 0;
	APHY_CSR_DDR_PLL_GLOBAL_RESET = 0;
	APHY_CSR_DDR_PLL_POST_DIV_RESET = 0;

	APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL0 = (1 << 16) | 0x53;
	APHY_CSR_DDR_PLL_VCO_FREQ_CNTRL1 = 0;
	APHY_CSR_DDR_PLL_MDIV_VALUE = 0;

	APHY_CSR_DDR_PLL_GLOBAL_RESET = 1;
	while (!(APHY_CSR_DDR_PLL_LOCK_STATUS & (1 << 16))) {
	}
	APHY_CSR_DDR_PLL_POST_DIV_RESET = 1;
	logf("DDR PLL locked, %u kHz\n", 19200u * 0x53 / 4);

	clkman_update_begin();
	CM_SDCCTL = CM_PASSWORD | (ctrl << CM_SDCCTL_CTRL_LSB) | (CM_SDCCTL & CM_SDCCTL_CTRL_CLR);
	clkman_update_end();

	SD_SA = (t->tREFI << SD_SA_RFSH_T_LSB)
	        | SD_SA_PGEHLDE_SET
	        | SD_SA_CLKSTOP_SET
	        | SD_SA_POWSAVE_SET
	        | 0x3214;

	SD_SB = SD_SB_REORDER_SET
	        | (t->banklow << SD_SB_BANKLOW_LSB)
	        | SD_SB_EIGHTBANK_SET
	        | (t->rowbits << SD_SB_ROWBITS_LSB)
	        | (t->colbits << SD_SB_COLBITS_LSB);

	logf("addressing: bank %u row %u col %u, SB 0x%X\n",
	     t->banklow, t->rowbits, t->colbits, SD_SB);

	SD_SC = (t->tRFCab << SD_SC_T_RFC_LSB)
	        | (t->tRRD << SD_SC_T_RRD_LSB)
	        | (t->tWR << SD_SC_T_WR_LSB)
	        | (t->tWTR << SD_SC_T_WTR_LSB)
	        | (3 << SD_SC_WL_LSB);

	SD_SD = (t->tRPab << SD_SD_T_RPab_LSB)
	        | (t->tRC << SD_SD_T_RC_LSB)
	        | (t->tXP << SD_SD_T_XP_LSB)
	        | (t->tRASmin << SD_SD_T_RAS_LSB)
	        | (t->tRPpb << SD_SD_T_RPpb_LSB)
	        | (t->tRCD << SD_SD_T_RCD_LSB);

	SD_SE = (1 << SD_SE_RL_EN_LSB)
	        | (4 << SD_SE_RL_LSB)
	        | (t->tFAW << SD_SE_T_FAW_LSB)
	        | (t->tRTP << SD_SE_T_RTP_LSB)
	        | (t->tXSR << SD_SE_T_XSR_LSB);

	SD_PT1 = (t->tINIT3 << SD_PT1_T_INIT3_LSB) | (t->tINIT1 << SD_PT1_T_INIT1_LSB);
	SD_PT2 = t->tINIT5 << SD_PT2_T_INIT5_LSB;
	SD_MRT = 0x3 << SD_MRT_T_MRW_LSB;

	reset_phy_dll();

	while (APHY_CSR_GLBL_ADR_DLL_LOCK_STAT != 3) {
	}

	APHY_CSR_PHY_BIST_CNTRL_SPR = 0x0;

	/* controller on */
	SD_CS = (((4 << SD_CS_ASHDN_T_LSB) | SD_CS_STATEN_SET | SD_CS_EN_SET)
	         & ~(SD_CS_STOP_SET | SD_CS_STBY_SET)) | SD_CS_RESTRT_SET;
}

static uint32_t read_mr(uint32_t addr) {
	while ((SD_MR & SD_MR_DONE_SET) != SD_MR_DONE_SET) {
	}
	SD_MR = addr & 0xff;
	uint32_t mrr;
	while (((mrr = SD_MR) & SD_MR_DONE_SET) != SD_MR_DONE_SET) {
	}
	return mrr;
}

static void write_mr(uint32_t addr, uint32_t data, bool wait) {
	while ((SD_MR & SD_MR_DONE_SET) != SD_MR_DONE_SET) {
	}
	SD_MR = (addr & 0xff) | ((data & 0xff) << 8) | SD_MR_RW_SET;
	if (!wait)
		return;
	uint32_t mrr;
	while (((mrr = SD_MR) & SD_MR_DONE_SET) != SD_MR_DONE_SET) {
	}
	if (mrr & SD_MR_TIMEOUT_SET)
		panic("MR write timed out (addr %u data 0x%X)", addr, data);
}

static void reset_phy(void) {
	SD_PHYC = SD_PHYC_PHYRST_SET;
	udelay(64);
	SD_PHYC = 0;

	DPHY_CSR_DQ_PHY_MISC_CTRL = 0x7;
	DPHY_CSR_DQ_PAD_MISC_CTRL = 0x0;
	DPHY_CSR_BOOT_READ_DQS_GATE_CTRL = 0x11;

	reset_phy_dll();

	APHY_CSR_PHY_BIST_CNTRL_SPR = 0x0;
}

static void switch_to_cprman_clock(uint32_t source, uint32_t div) {
	CM_SDCDIV = CM_PASSWORD | (div << CM_SDCDIV_DIV_LSB);
	CM_SDCCTL = CM_PASSWORD | (CM_SDCCTL & CM_SDCCTL_SRC_CLR) | source;
	CM_SDCCTL |= CM_PASSWORD | CM_SDCCTL_ENAB_SET;
	while (!(CM_SDCCTL & CM_SDCCTL_BUSY_SET)) {
	}
}

static void init_clkman(void) {
	clkman_update_begin();
	CM_SDCCTL = CM_PASSWORD | (CM_SDCCTL & CM_SDCCTL_CTRL_CLR);
	clkman_update_end();
}

static void calibrate_pvt_early(void) {
	/* chips whose VPU version ends in 0x14_ need a weaker DQ slew */
	bool st = ((g_cpuid >> 4) & 0xfff) == 0x14;
	uint32_t dq_slew = st ? 2 : 3;

	/* upstream: "the spec says do not use this register" */
	write_mr(0xff, 0, true);
	/* RL = 6, WL = 3 */
	write_mr(LPDDR2_MR_DEVICE_FEATURE_2, 4, true);

	APHY_CSR_ADDR_PAD_DRV_SLEW_CTRL = 0x333;
	DPHY_CSR_DQ_PAD_DRV_SLEW_CTRL = (dq_slew << 8) | (dq_slew << 4) | 3;
	logf("cpuid 0x%X, DQ slew %u\n", g_cpuid, dq_slew);

	APHY_CSR_PHY_BIST_CNTRL_SPR = BIST_pvt;

	/* process/voltage/temperature compensation of the pads */
	APHY_CSR_ADDR_PVT_COMP_CTRL = PVT_calibrate_request;
	while (!(APHY_CSR_ADDR_PVT_COMP_STATUS & 2)) {
	}
	DPHY_CSR_DQ_PVT_COMP_CTRL = PVT_calibrate_request;
	while (!(DPHY_CSR_DQ_PVT_COMP_STATUS & 2)) {
	}

	APHY_CSR_PHY_BIST_CNTRL_SPR = 0x0;

	/* ZQ calibration command to the chip */
	uint32_t old_mrt = SD_MRT;
	SD_MRT = 20;
	SD_MR = LPDDR2_MR_CALIBRATION | (0xff << 8) | SD_MR_RW_SET | SD_MR_HI_Z_SET;
	while ((SD_MR & SD_MR_DONE_SET) != SD_MR_DONE_SET) {
	}
	SD_MRT = old_mrt;

	write_mr(LPDDR2_MR_IO_CONFIG, st ? 3 : 2, false);
}

#define PAT0 0xaaaaaaaau
#define PAT1 0xff00aa00u
#define PAT2 0x99999999u

/* writes and reads 16 KB at addr through the uncached alias */
static void selftest_at(uint32_t addr) {
	volatile uint32_t *ram = (volatile uint32_t *)(addr | ALIAS_UNCACHED);
	static const uint32_t pat[4] = { PAT0, PAT1, PAT2, PAT0 };

	for (uint32_t i = 0; i < 0x1000; i++)
		ram[i] = pat[i & 3] ^ addr;
	for (uint32_t i = 0; i < 0x1000; i++) {
		uint32_t want = pat[i & 3] ^ addr;
		if (ram[i] != want)
			panic("SDRAM test failed at 0x%X: wrote 0x%X, read 0x%X",
			      addr + i * 4, want, ram[i]);
	}
}

static void selftest(void) {
	static const uint32_t size_mb[] = { 1024, 512, 256, 128 };
	uint32_t mb = size_mb[g_ram_size];
	uint32_t start = timer_us();

	/* first and last 16 KB of every 64 MB; addresses mixed into the pattern
	 * so an address line fault shows up as well */
	for (uint32_t base = 0; base < mb; base += 64) {
		selftest_at(base << 20);
		selftest_at(((base + 64) << 20) - 0x4000);
	}
	logf("self test passed, %u MB, %u regions, %u us\n",
	     mb, 2 * mb / 64, timer_us() - start);
}

void sdram_init(void) {
	logf("SD_CS 0x%X at start\n", SD_CS);

	PM_SMPS = PM_PASSWORD | 0x1;
	A2W_SMPS_LDO1 = A2W_PASSWORD | 0x40000;
	A2W_SMPS_LDO0 = A2W_PASSWORD | 0x0;

	A2W_XOSC_CTRL |= A2W_PASSWORD | A2W_XOSC_CTRL_DDREN_SET;

	/* step 1: slow clock from the oscillator, controller up, calibrate */
	switch_to_cprman_clock(CM_SRC_OSC, 1);
	init_clkman();
	reset_phy();

	/* start-up values as upstream */
	SD_SA = 0x006e3395;
	SD_SB = 0x0f9;
	SD_SC = 0x6000431;
	SD_SD = 0x10000011;
	SD_SE = 0x10106000;
	SD_PT1 = 0x0af002;
	SD_PT2 = 0x8c;
	SD_MRT = 0x3;
	SD_CS = 0x200042;

	while (!(SD_CS & SD_CS_SDUP_SET)) {
	}
	logf("controller up, SD_CS 0x%X\n", SD_CS);

	write_mr(LPDDR2_MR_DEVICE_FEATURE_2, 4, false);
	calibrate_pvt_early();

	uint32_t vendor = read_mr(LPDDR2_MR_MANUFACTURER_ID);
	if (!MR_REQUEST_SUCCESS(vendor))
		panic("SDRAM vendor ID read timed out");
	vendor = MR_GET_RDATA(vendor);

	uint32_t mr8 = read_mr(LPDDR2_MR_METRICS);
	if (!MR_REQUEST_SUCCESS(mr8))
		panic("SDRAM MR8 read timed out");
	mr8 = MR_GET_RDATA(mr8);

	g_ram_size = lpddr2_size(mr8);
	logf("%s %s LPDDR2 (MR5 0x%X, MR8 0x%X)\n",
	     lpddr2_manufacturer_name(vendor), size_name[g_ram_size], vendor, mr8);
	if (g_ram_size == RAM_UNKNOWN)
		panic("unknown SDRAM size (MR8 0x%X)", mr8);

	/* step 2: full speed from the PHY's DDR PLL, real timings. We run
	 * from the L2 cache, so changing the SDRAM clock is safe here. */
	if (g_ram_size == RAM_1GB) {
		timings.colbits = 3;
		timings.rowbits = 3;
		timings.banklow = 3;
	} else if (g_ram_size == RAM_512MB) {
		timings.colbits = 2;
	}

	reset_with_timing(&timings);
	selftest();
}

/*
 * redox-rpi bootcode: SD card through the SDHOST controller (#230 M2).
 *
 * Read-only, 1-bit bus, one 512-byte sector at a time. The card sits on
 * GPIO 48-53, where the Boot ROM read bootcode.bin from.
 *
 * Sequence and values from rpi-open-firmware (GPLv2+),
 * arm_chainloader/drivers/sdhost_impl.cc, with our fix from Pi test #249
 * (on a transfer error, halve the data clock and read again), rewritten
 * in C for the VPU. Command numbers and the CSD/CID fields from the SD
 * specification as in NetBSD's sdmmcreg.h (ISC licence). Register names
 * from Broadcom's sdhost.h (BSD).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define CMD_GO_IDLE 0
#define CMD_ALL_SEND_CID 2
#define CMD_SET_RCA 3
#define CMD_SELECT 7
#define CMD_SEND_IF_COND 8
#define CMD_SEND_CSD 9
#define CMD_STOP 12
#define CMD_SET_BLOCKLEN 16
#define CMD_READ_MULTIPLE 18
#define ACMD_OP_COND 41
#define CMD_APP 55

#define OCR_READY (1u << 31)
#define OCR_HCS (1u << 30)
#define OCR_3V3 (1u << 21)

#define HSTS_ERRORS (SH_HSTS_CMD_TIME_OUT_SET | SH_HSTS_CRC7_ERROR_SET | \
                     SH_HSTS_CRC16_ERROR_SET | SH_HSTS_REW_TIME_OUT_SET | SH_HSTS_FIFO_ERROR_SET)
#define CDIV_IDENT 0x148 /* about 750 kHz from the core clock */
#define EDM_THRESHOLD_MASK 0x1f
#define EDM_READ_SHIFT 14
#define EDM_WRITE_SHIFT 9

struct sd_card g_sd;
static uint32_t resp[4];

/* bits start..start+len-1 of a 128-bit response, bit 0 = resp[0] bit 0 */
static uint32_t rbits(const uint32_t *r, unsigned start, unsigned len) {
	uint32_t v = 0;
	for (unsigned i = 0; i < len; i++) {
		unsigned b = start + i;
		v |= ((r[b / 32] >> (b % 32)) & 1u) << i;
	}
	return v;
}

static bool cmd_wait(void) {
	for (uint32_t t = 0; SH_CMD & SH_CMD_NEW_FLAG_SET; t++) {
		if (t >= 100000)
			return false;
		udelay(10);
	}
	return true;
}

static void cmd_send(uint32_t cmd, uint32_t arg) {
	cmd_wait();
	uint32_t sts = SH_HSTS;
	if (sts & HSTS_ERRORS)
		SH_HSTS = sts;
	SH_ARG = arg;
	SH_CMD = cmd | SH_CMD_NEW_FLAG_SET;
}

/* wait for the command, read the response; false on a timeout or error */
static bool cmd_done(void) {
	if (!cmd_wait())
		return false;
	resp[0] = SH_RSP0;
	resp[1] = SH_RSP1;
	resp[2] = SH_RSP2;
	resp[3] = SH_RSP3;
	return !(SH_CMD & SH_CMD_FAIL_FLAG_SET);
}

static bool cmd(uint32_t c, uint32_t arg) {
	cmd_send(c & SH_CMD_COMMAND_SET, arg);
	return cmd_done();
}

static void pins_and_reset(void) {
	/* GPIO 48-53 to ALT0 (SDHOST), pull-ups on */
	GP_FSEL4 = (GP_FSEL4 & 0x00ffffff) | 0x24000000;
	GP_FSEL5 = (GP_FSEL5 & ~0xfffu) | 0x924;
	GP_PUD = 2;
	udelay(500);
	GP_PUDCLK1 = 0x003f0000u; /* GPIO 48-53 */
	udelay(500);
	GP_PUD = 0;
	GP_PUDCLK1 = 0;

	SH_VDD = 0;
	SH_CMD = 0;
	SH_ARG = 0;
	SH_TOUT = 0xf00000;
	SH_CDIV = 0;
	SH_HSTS = 0x7f8;
	SH_HCFG = 0;
	SH_HBCT = 0;
	SH_HBLC = 0;
	uint32_t edm = SH_EDM & ~((EDM_THRESHOLD_MASK << EDM_READ_SHIFT) | (EDM_THRESHOLD_MASK << EDM_WRITE_SHIFT));
	SH_EDM = edm | (4u << EDM_READ_SHIFT) | (4u << EDM_WRITE_SHIFT);
	udelay(300);
	SH_VDD = SH_VDD_POWER_ON_SET;
	udelay(300);
	SH_HCFG = SH_HCFG_SLOW_CARD_SET | SH_HCFG_WIDE_INT_BUS_SET;
	SH_CDIV = CDIV_IDENT;
	udelay(300);
}

static void drain_fifo(void) {
	cmd_wait();
	while (SH_HSTS & SH_HSTS_DATA_FLAG_SET)
		(void)SH_DATA;
}

static bool read_once(uint32_t lba, uint32_t *buf) {
	drain_fifo();
	cmd_send(CMD_READ_MULTIPLE | SH_CMD_READ_CMD_SET, g_sd.high_capacity ? lba : lba << 9);
	uint32_t err = 0;
	for (int i = 0; i < 128 && !err; i++) {
		uint32_t t = 0;
		while (!(SH_HSTS & SH_HSTS_DATA_FLAG_SET)) {
			if (++t > 100000) {
				err = 0xffffffffu;
				break;
			}
			udelay(10);
		}
		if (err)
			break;
		err = SH_HSTS & HSTS_ERRORS;
		if (err)
			break;
		uint32_t w = SH_DATA;
		if (buf)
			buf[i] = w;
	}
	cmd_send(CMD_STOP | SH_CMD_BUSY_CMD_SET, 0);
	cmd_wait();
	return err == 0;
}

/* read one sector; on an error halve the data clock and retry (Pi test #249) */
bool sd_read(uint32_t lba, uint32_t *buf) {
	if (!g_sd.ready)
		return false;
	for (int attempt = 0; attempt < 5; attempt++) {
		if (read_once(lba, buf))
			return true;
		uint32_t cdiv = SH_CDIV;
		if (cdiv >= CDIV_IDENT)
			break;
		SH_CDIV = (cdiv + 2) * 2 - 2;
		printf("sd: read of sector %u failed (HSTS %08X), SH_CDIV %X -> %X\n", lba, SH_HSTS, cdiv, SH_CDIV);
	}
	return false;
}

/*
 * Power the controller down before the ARM takes over, so the OS driver
 * finds it idle (no identification clock running). Order from glennra's
 * arm-handoff-review branch of rpi-open-firmware, where leaving it on made
 * Linux's probe intermittent.
 */
void sd_off(void) {
	if (!g_sd.ready)
		return;
	drain_fifo();
	SH_VDD = 0;
	udelay(1000);
	SH_HCFG = 0;
	SH_CMD = 0;
	SH_ARG = 0;
	SH_CDIV = 0;
	SH_TOUT = 0;
	SH_HBCT = 0;
	SH_HBLC = 0;
	SH_HSTS = 0x7f8;
	g_sd.ready = false;
}

bool sd_init(void) {
	g_sd.ready = false;
	g_sd.high_capacity = false;
	pins_and_reset();

	cmd_send(CMD_GO_IDLE | SH_CMD_NO_RESPONSE_SET, 0);
	cmd_wait();
	cmd(CMD_SEND_IF_COND, 0x1aa);
	uint32_t ocr = OCR_3V3 | (resp[0] == 0x1aa ? OCR_HCS : 0);
	for (int i = 0;; i++) {
		cmd(CMD_APP, 0);
		cmd_send(ACMD_OP_COND | SH_CMD_NO_RESPONSE_SET, ocr);
		if (!cmd_done()) {
			printf("err sd: no answer to ACMD41\n");
			return false;
		}
		if (resp[0] & OCR_READY)
			break;
		if (i > 10000) {
			printf("err sd: card never ready, OCR %08X\n", resp[0]);
			return false;
		}
		udelay(100);
	}
	g_sd.high_capacity = (resp[0] & OCR_HCS) != 0;

	cmd_send(CMD_ALL_SEND_CID | SH_CMD_LONG_RESPONSE_SET, 0);
	if (!cmd_done()) {
		printf("err sd: no CID\n");
		return false;
	}
	for (int i = 0; i < 5; i++)
		g_sd.name[i] = (char)rbits(resp, 96 - 8 * i, 8);
	g_sd.name[5] = 0;
	if (!cmd(CMD_SET_RCA, 0)) {
		printf("err sd: no RCA\n");
		return false;
	}
	uint32_t rca = resp[0] >> 16;
	cmd_send(CMD_SEND_CSD | SH_CMD_LONG_RESPONSE_SET, rca << 16);
	if (!cmd_done()) {
		printf("err sd: no CSD\n");
		return false;
	}
	uint32_t csdver = rbits(resp, 126, 2), div;
	if (csdver == 1) {
		g_sd.mb = (rbits(resp, 48, 22) + 1) / 2; /* (C_SIZE + 1) * 512 KiB */
		div = 5;
	} else {
		uint32_t blocks = (rbits(resp, 62, 12) + 1) << (rbits(resp, 47, 3) + 2);
		g_sd.mb = (blocks >> 11) << rbits(resp, 80, 4) >> 9;
		div = 10;
	}
	cmd(CMD_SELECT, rca << 16);
	if (csdver != 1)
		cmd(CMD_SET_BLOCKLEN, 512);
	SH_CDIV = div - 2; /* 250 MHz core clock assumed, as upstream: 50 or 25 MHz */
	g_sd.ready = true;
	/* upstream reads three sectors and drops them before the first real read */
	for (int i = 0; i < 3; i++)
		sd_read(0, NULL);
	return true;
}

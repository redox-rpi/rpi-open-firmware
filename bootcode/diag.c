/*
 * diag.c: a sampler for finding display and graphics stalls (rof #66).
 *
 * While the ARM runs, the VPU only answers the mailbox. When the ARM asks
 * (mailbox tag 0x000f0001), config.txt has diag_period=US (started before
 * the ARM) or the monitor's "diag" command starts it, the VPU also
 * writes one record per period into a ring buffer in the VideoCore's
 * memory, which the ARM can read: 2 MB into the VideoCore's part
 * (g_arm_mem), between the boot screen panel and the ARM's framebuffer
 * (mbox.c FB_BASE, 4 MB in). Not at the end of the SDRAM: on a 1 GB
 * board the ARM sees its last 16 MB as the peripherals at 0x3f000000
 * (Pi test 516).
 *
 *   header, 16 words: magic "DIAG", version, record words, records in the
 *     ring, records written (wraps the ring; never reset while running),
 *     period in us, marker (the ARM writes it), dropped samples
 *   record, 8 words: system timer (us), HVS DISPSTAT, HVS DISPSTAT1
 *     (channel 1, with its frame count), pixel valve 2 STAT, HDMI frame
 *     count, thermal sensor status (raw), ARM clock word (bits 31:30 the
 *     ARM channel divider, 29:20 PLLB ndiv, 19:0 frac),
 *     marker as it was at this sample
 *
 * The devicetree's /chosen redox-rpi,diag-ring = <ARM address, bytes>
 * names the ring (boot64.c), so a reader (redox-rpi tools/diagread, Redox
 * and Linux) needs no mailbox to find it.
 *
 * No V3D counters yet: our firmware does not power V3D (M5). The AXI bus
 * monitors come next, once their registers are confirmed.
 *
 * Registers: HVS and pixel valve as in hdmi.c (Linux vc4_regs.h); the
 * thermal sensor at 0x7e212000 (Linux bcm2835_thermal.c: TS_TSENSCTL,
 * TS_TSENSSTAT); PLLB as arm.c sets it.
 */
#include "hw.h"

#define R(a) HW_REGISTER_RW(a)
#define D_HVS_DISPSTAT R(0x7e400004)
#define D_HVS_DISPSTAT1 R(0x7e400058)
#define D_PV2_STAT R(0x7e80702c)
#define D_HD_FRAME_COUNT R(0x7e808068)
#define D_TS_CTL R(0x7e212000)
#define D_TS_STAT R(0x7e212004)
#define D_CM_TSENSCTL R(0x7e1010e0)
#define D_CM_TSENSDIV R(0x7e1010e4)

#define DIAG_MAGIC 0x47414944u /* "DIAG" little-endian */
#define DIAG_VERSION 1u
#define DIAG_HDR_WORDS 16u
#define DIAG_REC_WORDS 8u
#define DIAG_BYTES 0x100000u /* 1 MB */
#define DIAG_OFF 0x200000u  /* into the VideoCore's part, below FB_BASE */
#define DIAG_RECS ((DIAG_BYTES / 4 - DIAG_HDR_WORDS) / DIAG_REC_WORDS)

enum { H_MAGIC, H_VERSION, H_REC_WORDS, H_RECS, H_WRITTEN, H_PERIOD, H_MARKER, H_DROPPED };

static void ts_start(void);
static bool g_diag_on;
static uint32_t g_diag_next, g_diag_period;

static volatile uint32_t *diag_ring(void) {
	/* uncached alias, so the ARM sees every word at once */
	return (volatile uint32_t *)(0xc0000000u | (g_arm_mem + DIAG_OFF));
}

uint32_t diag_bus_addr(void) {
	return 0xc0000000u | (g_arm_mem + DIAG_OFF);
}

uint32_t diag_bytes(void) {
	return DIAG_BYTES;
}

/* period 0 stops; otherwise clears the ring and starts at once */
void diag_start(uint32_t period_us) {
	volatile uint32_t *h = diag_ring();
	if (!period_us) {
		g_diag_on = false;
		return;
	}
	if (period_us < 100)
		period_us = 100;
	for (uint32_t i = 0; i < DIAG_HDR_WORDS; i++)
		h[i] = 0;
	h[H_VERSION] = DIAG_VERSION;
	h[H_REC_WORDS] = DIAG_REC_WORDS;
	h[H_RECS] = DIAG_RECS;
	h[H_PERIOD] = period_us;
	h[H_MAGIC] = DIAG_MAGIC;
	ts_start();
	g_diag_period = period_us;
	g_diag_next = timer_us();
	g_diag_on = true;
}

/*
 * The thermal sensor, as Broadcom's firmware leaves it (Pi test 337) and
 * Linux bcm2835_thermal.c starts it: its clock at 19.2 MHz / 10 from the
 * crystal, then TSENSCTL with the bandgap default, the regulator and the
 * recommended reset delay, written once without and once with RSTB. Before
 * this the sensor had no clock and read a fixed 268 C (Pi test 530).
 */
static void ts_start(void) {
	if (!(D_CM_TSENSCTL & 0x10u)) { /* ENAB */
		D_CM_TSENSDIV = CM_PASSWORD | 0xa000u;
		D_CM_TSENSCTL = CM_PASSWORD | 1u;         /* source: crystal */
		D_CM_TSENSCTL = CM_PASSWORD | 0x10u | 1u; /* enable */
	}
	if (!(D_TS_CTL & 2u)) { /* RSTB */
		uint32_t v = 1u << 2 | 1u << 26 | 0xfeu << 18 | 607u << 8; /* threshold 80 C */
		D_TS_CTL = v;
		D_TS_CTL = v | 2u;
	}
}

/* called from the mailbox loop: one record when the period is due */
void diag_poll(void) {
	if (!g_diag_on)
		return;
	uint32_t now = timer_us();
	if ((int32_t)(now - g_diag_next) < 0)
		return;
	volatile uint32_t *h = diag_ring();
	g_diag_next += g_diag_period;
	if ((int32_t)(now - g_diag_next) >= 0) { /* fell behind: count, don't catch up */
		h[H_DROPPED] += (now - g_diag_next) / g_diag_period + 1;
		g_diag_next = now + g_diag_period;
	}
	uint32_t n = h[H_WRITTEN];
	volatile uint32_t *r = h + DIAG_HDR_WORDS + (n % DIAG_RECS) * DIAG_REC_WORDS;
	uint32_t marker = h[H_MARKER];
	r[0] = now;
	r[1] = D_HVS_DISPSTAT;
	r[2] = D_HVS_DISPSTAT1;
	r[3] = D_PV2_STAT;
	r[4] = D_HD_FRAME_COUNT;
	r[5] = D_TS_STAT;
	/* ARM channel divider (1 or 2, arm.c) in bits 31:30, PLLB ndiv, frac */
	r[6] = (A2W_PLLB_ARM & 3u) << 30 | (A2W_PLLB_CTRL & 0x3ffu) << 20 | (A2W_PLLB_FRAC & 0xfffffu);
	r[7] = marker;
	h[H_WRITTEN] = n + 1;
}

/* monitor "diagdump N": the last N records, oldest first */
void diag_dump(uint32_t count) {
	volatile uint32_t *h = diag_ring();
	uint32_t n = h[H_WRITTEN];
	if (h[H_MAGIC] != DIAG_MAGIC) {
		printf("err diag: not started\n");
		return;
	}
	if (count > n)
		count = n;
	if (count > DIAG_RECS)
		count = DIAG_RECS;
	printf("diag: at %08X, %u written, period %u us, dropped %u\n", diag_bus_addr(), n, h[H_PERIOD],
	       h[H_DROPPED]);
	for (uint32_t i = n - count; i != n; i++) {
		volatile uint32_t *r = h + DIAG_HDR_WORDS + (i % DIAG_RECS) * DIAG_REC_WORDS;
		printf("%u %08X %08X %08X %08X %08X %08X %08X\n", r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
	}
	printf("ok diag %u records\n", count);
}

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
 * AXI bus counters (bit 31 of the period word, DIAG_AXI): the two AXI
 * performance monitors, "system" at 0x7e009800 (the ARM can reach it too)
 * and "VPU" at 0x7ee08000 (only the VPU can), three bus watchers each.
 * Each record then has 32 words: after the 8 above, per watcher (system
 * HVS, CPU L2, V3D; VPU SDRAM, L2 in, DMA uncached) read transactions,
 * write transactions and read+write wait cycles since the last record,
 * then 6 words left 0 for V3D counters. Layout as Raspberry Pi's Linux
 * drivers/perf/raspberrypi_axi_monitor.c (general control at 0, watcher i
 * at 0x40 + 0x40 * i, results at +0x04..+0x2c; bus numbers from its
 * system_bus_string and vpu_bus_string tables); code our own. Header word
 * 8 has the flags (bit 0: AXI), word 9 the six bus numbers, 5 bits each.
 *
 * No V3D counters yet: our firmware does not power V3D (M5).
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
#define D_TS_STAT R(0x7e212004)

#define DIAG_MAGIC 0x47414944u /* "DIAG" little-endian */
#define DIAG_VERSION 2u
#define DIAG_HDR_WORDS 16u
#define DIAG_REC_WORDS 8u
#define DIAG_REC_WORDS_AXI 32u
#define DIAG_BYTES 0x100000u /* 1 MB */
#define DIAG_OFF 0x200000u  /* into the VideoCore's part, below FB_BASE */

enum { H_MAGIC, H_VERSION, H_REC_WORDS, H_RECS, H_WRITTEN, H_PERIOD, H_MARKER, H_DROPPED, H_FLAGS, H_AXI_BUSES };

/* AXI performance monitors: general control, bus watchers */
#define AXI_SYS 0x7e009800u
#define AXI_VPU 0x7ee08000u
#define AXI_GEN_ENABLE 0x1u
#define AXI_GEN_RESET 0x2u
#define AXI_GEN_WATCH 0x4u
#define AXI_BW(base, i) ((base) + 0x40u + 0x40u * (i))
#define AXI_BW_RESET 0x80000000u
#define AXI_BW_ENABLE 0x40000000u
/* results, from the watcher's control register */
#define AXI_WTRANS 0x10u
#define AXI_WTWAIT 0x14u
#define AXI_RTRANS 0x1cu
#define AXI_RTWAIT 0x20u
/* system: HVS 9, CPU_L2 15, V3D 12; VPU: SDRAM 14, L2_IN 15, DMA_UC 13 */
static const uint32_t axi_bus[6] = {9, 15, 12, 14, 15, 13};

static bool g_diag_on;
static bool g_diag_axi;
static uint32_t g_diag_next, g_diag_period, g_diag_rec_words, g_diag_recs;

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

static uint32_t axi_base(uint32_t w) {
	return w < 3 ? AXI_SYS : AXI_VPU;
}

/* watcher w (0..5) back to 0 and counting its bus */
static void axi_arm(uint32_t w) {
	uint32_t c = AXI_BW(axi_base(w), w % 3);
	R(c) = AXI_BW_RESET;
	R(c) = AXI_BW_ENABLE | axi_bus[w];
}

static void axi_on(bool on) {
	R(AXI_SYS) = AXI_GEN_RESET;
	R(AXI_VPU) = AXI_GEN_RESET;
	if (!on) {
		R(AXI_SYS) = 0;
		R(AXI_VPU) = 0;
		return;
	}
	for (uint32_t w = 0; w < 6; w++)
		axi_arm(w);
	R(AXI_SYS) = AXI_GEN_ENABLE | AXI_GEN_WATCH;
	R(AXI_VPU) = AXI_GEN_ENABLE | AXI_GEN_WATCH;
}

/* period 0 stops; otherwise clears the ring and starts at once; bit 31
 * (DIAG_AXI) adds the AXI bus counters */
void diag_start(uint32_t word) {
	volatile uint32_t *h = diag_ring();
	uint32_t period_us = word & ~DIAG_AXI;
	if (g_diag_axi && (!period_us || !(word & DIAG_AXI)))
		axi_on(false);
	if (!period_us) {
		g_diag_on = false;
		g_diag_axi = false;
		return;
	}
	g_diag_axi = (word & DIAG_AXI) != 0;
	g_diag_rec_words = g_diag_axi ? DIAG_REC_WORDS_AXI : DIAG_REC_WORDS;
	g_diag_recs = (DIAG_BYTES / 4 - DIAG_HDR_WORDS) / g_diag_rec_words;
	if (period_us < 100)
		period_us = 100;
	for (uint32_t i = 0; i < DIAG_HDR_WORDS; i++)
		h[i] = 0;
	h[H_VERSION] = DIAG_VERSION;
	h[H_REC_WORDS] = g_diag_rec_words;
	h[H_RECS] = g_diag_recs;
	h[H_FLAGS] = g_diag_axi ? 1u : 0u;
	for (uint32_t w = 0; w < 6; w++)
		h[H_AXI_BUSES] |= axi_bus[w] << (5 * w);
	h[H_PERIOD] = period_us;
	h[H_MAGIC] = DIAG_MAGIC;
	thermal_start(); /* thermal.c */
	g_diag_period = period_us;
	if (g_diag_axi)
		axi_on(true);
	g_diag_next = timer_us();
	g_diag_on = true;
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
	volatile uint32_t *r = h + DIAG_HDR_WORDS + (n % g_diag_recs) * g_diag_rec_words;
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
	if (g_diag_axi) {
		for (uint32_t w = 0; w < 6; w++) {
			uint32_t c = AXI_BW(axi_base(w), w % 3);
			r[8 + 3 * w] = R(c + AXI_RTRANS);
			r[9 + 3 * w] = R(c + AXI_WTRANS);
			r[10 + 3 * w] = R(c + AXI_RTWAIT) + R(c + AXI_WTWAIT);
			axi_arm(w);
		}
		for (uint32_t i = 26; i < 32; i++)
			r[i] = 0;
	}
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
	uint32_t recs = h[H_RECS], words = h[H_REC_WORDS];
	if (count > recs)
		count = recs;
	printf("diag: at %08X, %u written, period %u us, dropped %u\n", diag_bus_addr(), n, h[H_PERIOD],
	       h[H_DROPPED]);
	for (uint32_t i = n - count; i != n; i++) {
		volatile uint32_t *r = h + DIAG_HDR_WORDS + (i % recs) * words;
		printf("%u %08X %08X %08X %08X %08X %08X %08X", r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
		for (uint32_t k = 8; k < 26 && words > 8; k++)
			printf(" %u", r[k]);
		printf("\n");
	}
	printf("ok diag %u records\n", count);
}

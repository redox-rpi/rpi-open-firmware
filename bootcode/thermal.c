/*
 * thermal.c: the SoC temperature and the ARM clock throttle (rof #108).
 *
 * The sensor at 0x7e212000 as Linux bcm2835_thermal.c reads it: TSENSSTAT
 * bits 9:0 the reading, bit 10 valid; millidegrees = 407000 - 538 * reading
 * (the bcm283x devicetree's coefficients). It is started as Broadcom's
 * firmware leaves it (Pi tests 337, 530; it moved here from diag.c).
 *
 * While the OS runs, the mailbox loop calls thermal_poll(). Every 250 ms
 * it reads the sensor: from 5 C below temp_limit (85 C, or lower from
 * config.txt) the ARM clock's upper limit goes down by THR_STEP_MHZ per
 * check, down to 600 MHz; 10 C below temp_limit it goes up again by the same step until the limit is gone. The OS's own rate
 * (mailbox tag 0x00038002) is kept and comes back when the limit allows
 * it. This is a safety net for an OS without its own thermal management;
 * it does not change the core voltage (there is no voltage control).
 *
 * The thresholds follow Broadcom's firmware (behaviour only): it throttles
 * from about 80 C; 85 C is the chip's limit and what tag 0x0003000a says.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifdef THERMAL_HOST
#include <stdbool.h>
#include <stdint.h>
#else
#include "hw.h"
#endif

#define THR_LIMIT_MC 85000 /* temp_limit's default and maximum */
#define THR_HOT_BELOW 5000 /* throttling starts this far below the limit */
#define THR_COOL_BELOW 10000 /* and the limit goes up again below this */
#define THR_STEP_MHZ 100u
#define THR_MIN_MHZ 600u
#define THR_PERIOD_US 250000u

/* millidegrees from a TSENSSTAT word, or false when it is not valid */
bool thermal_mc_from_stat(uint32_t stat, int32_t *mc) {
	if (!(stat & 0x400u))
		return false;
	*mc = 407000 - 538 * (int32_t)(stat & 0x3ffu);
	return true;
}

/* the next upper limit for the ARM clock, from the last one and the temperature */
uint32_t thermal_next_cap(uint32_t cap, int32_t mc, int32_t limit_mc, uint32_t max_mhz) {
	if (mc >= limit_mc - THR_HOT_BELOW)
		cap = cap > THR_MIN_MHZ + THR_STEP_MHZ ? cap - THR_STEP_MHZ : THR_MIN_MHZ;
	else if (mc < limit_mc - THR_COOL_BELOW)
		cap = cap + THR_STEP_MHZ < max_mhz ? cap + THR_STEP_MHZ : max_mhz;
	if (cap > max_mhz)
		cap = max_mhz;
	return cap;
}

#ifndef THERMAL_HOST
#define T_TS_CTL HW_REGISTER_RW(0x7e212000)
#define T_TS_STAT HW_REGISTER_RW(0x7e212004)

static bool g_started;
static int32_t g_mc = 0;      /* last valid reading; 0 = none yet */
static uint32_t g_cap;        /* upper limit in MHz; 0 = none */
static uint32_t g_want;       /* the OS's rate in MHz; 0 = the boot rate */
static uint32_t g_flags;      /* tag 0x00030046 */
static uint32_t g_boot;       /* the boot rate in MHz; 0 = not read yet */
static uint32_t g_next;
static int32_t g_limit = THR_LIMIT_MC;

/* temp_limit= from config.txt, in C: lower only, as Broadcom's firmware */
void thermal_set_limit(uint32_t c) {
	if (c < 40u || c > THR_LIMIT_MC / 1000u) {
		printf("thermal: temp_limit=%u ignored (40..85)\n", c);
		return;
	}
	g_limit = (int32_t)c * 1000;
	printf("thermal: temp_limit %u C, throttling from %u C\n", c, c - THR_HOT_BELOW / 1000u);
}

int32_t thermal_limit_mc(void) {
	return g_limit;
}

/*
 * Its clock at 19.2 MHz / 10 from the crystal, then TSENSCTL with the
 * bandgap default, the regulator and the recommended reset delay, written
 * once without and once with RSTB. Before this the sensor had no clock and
 * read a fixed 268 C (Pi test 530).
 */
void thermal_start(void) {
	if (g_started)
		return;
	if (!(CM_TSENSCTL & 0x10u)) { /* ENAB */
		CM_TSENSDIV = CM_PASSWORD | 0xa000u;
		CM_TSENSCTL = CM_PASSWORD | 1u;         /* source: crystal */
		CM_TSENSCTL = CM_PASSWORD | 0x10u | 1u; /* enable */
	}
	if (!(T_TS_CTL & 2u)) { /* RSTB */
		uint32_t v = 1u << 2 | 1u << 26 | 0xfeu << 18 | 607u << 8; /* threshold 80 C */
		T_TS_CTL = v;
		T_TS_CTL = v | 2u;
	}
	g_started = true;
	g_next = timer_us();
}

/* millidegrees, or 0 before the first valid reading */
int32_t thermal_mc(void) {
	int32_t mc;
	thermal_start();
	if (thermal_mc_from_stat(T_TS_STAT, &mc))
		g_mc = mc;
	return g_mc;
}

uint32_t thermal_flags(void) {
	return g_flags;
}

/* the OS's rate, or the boot rate while the OS has set none */
static uint32_t want(void) {
	if (!g_boot)
		g_boot = (arm_khz() + 500u) / 1000u;
	return g_want ? g_want : g_boot;
}

/* the rate the ARM should run at: the wanted one, at most the limit */
static uint32_t target(void) {
	uint32_t w = want();
	return g_cap && g_cap < w ? g_cap : w;
}

/* the OS's rate (tag 0x00038002): returns the ARM clock in kHz */
uint32_t thermal_set_arm_mhz(uint32_t mhz) {
	want();
	g_want = mhz;
	return arm_set_mhz(target());
}

void thermal_poll(void) {
	thermal_start();
	uint32_t now = timer_us();
	if ((int32_t)(now - g_next) < 0)
		return;
	g_next = now + THR_PERIOD_US;
	int32_t mc;
	if (!thermal_mc_from_stat(T_TS_STAT, &mc))
		return; /* no valid reading: leave the clock alone */
	g_mc = mc;
	if (!g_cap && mc < g_limit - THR_HOT_BELOW)
		return; /* not hot, not throttled */
	uint32_t cap = thermal_next_cap(target(), mc, g_limit, arm_max_mhz);
	if (cap >= want())
		cap = 0; /* cool again: no limit */
	if (cap == g_cap)
		return;
	g_cap = cap;
	if (cap)
		g_flags |= 1u << 1 | 1u << 2 | 1u << 17 | 1u << 18; /* capped, throttled; 17, 18: since boot */
	else
		g_flags &= ~(1u << 1 | 1u << 2);
	printf("thermal: %d C, ARM clock limit %u MHz\n", (int)(mc / 1000), cap ? cap : want());
	arm_set_mhz(target());
}
#endif

/*
 * Host test for thermal.c (rof #108): the sensor conversion and the steps
 * of the ARM clock limit. Run by thermal-test.sh.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define THERMAL_HOST
#include "../thermal.c"
#include <stdio.h>

static int fail;
#define CHECK(name, cond) do { if (cond) printf("ok   %s\n", name); \
	else { printf("FAIL %s\n", name); fail = 1; } } while (0)

int main(void) {
	int32_t mc = 0;
	CHECK("invalid reading is ignored", !thermal_mc_from_stat(607, &mc));
	CHECK("607 is about 80.4 C", thermal_mc_from_stat(0x400 | 607, &mc) && mc == 80434);
	CHECK("663 is about 50.3 C", thermal_mc_from_stat(0x400 | 663, &mc) && mc == 50306);
	CHECK("hot: 100 MHz down", thermal_next_cap(1200, 80000, 1200) == 1100);
	CHECK("hot: not below 600", thermal_next_cap(650, 90000, 1200) == 600);
	CHECK("hot at 600 stays 600", thermal_next_cap(600, 90000, 1200) == 600);
	CHECK("between 75 and 80 C: kept", thermal_next_cap(900, 77000, 1200) == 900);
	CHECK("cool: 100 MHz up", thermal_next_cap(900, 74999, 1200) == 1000);
	CHECK("cool: not above max", thermal_next_cap(1150, 50000, 1200) == 1200);
	CHECK("Zero 2 W max 1000", thermal_next_cap(1000, 50000, 1000) == 1000);
	/* a run: 939 MHz boot rate, heats to 85 C, then cools */
	uint32_t cap = 939;
	int32_t temps[] = {85000, 85000, 85000, 82000, 78000, 74000, 70000, 70000};
	uint32_t want[] = {839, 739, 639, 600, 600, 700, 800, 900};
	int ok = 1;
	for (unsigned i = 0; i < sizeof temps / sizeof temps[0]; i++) {
		cap = thermal_next_cap(cap, temps[i], 1200);
		if (cap != want[i]) { printf("     step %u: %u, want %u\n", i, cap, want[i]); ok = 0; }
	}
	CHECK("heat up and cool down", ok);
	return fail;
}

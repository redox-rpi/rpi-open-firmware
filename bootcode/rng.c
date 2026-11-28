/*
 * rng.c: start the hardware random number generator for the OS (rof #90).
 *
 * The RNG at 0x7e104000 (ARM 0x3f104000) as Linux bcm2835-rng.c starts it:
 * RNG_STATUS gets the warm-up count (the number of words the generator
 * throws away before the FIFO fills), then RNG_CTRL bit 0 (RBGEN) starts it.
 * The warm-up takes about 850 ms (Redox randd on the Zero 2 W, which starts
 * the RNG itself when RBGEN is clear). Started here, before the kernel and
 * the initramfs load, the warm-up is over when the OS asks for its first
 * words; randd sees RBGEN set and does not wait.
 *
 * Broadcom's start.elf leaves the RNG off; the OS starts it. Starting it
 * earlier changes nothing for an OS that starts it again: Linux and randd
 * both skip the start when RBGEN is already set.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

/* RNG_CTRL and RNG_STATUS come from broadcom/bcm2708_chip/rng.h */
#define RNG_RBGEN 0x1u
#define RNG_WARMUP_COUNT 0x40000u

void rng_start(void) {
	if (RNG_CTRL & RNG_RBGEN)
		return; /* already running */
	RNG_STATUS = RNG_WARMUP_COUNT;
	RNG_CTRL = RNG_RBGEN;
	printf("rng: started, warm-up %u words\n", RNG_WARMUP_COUNT);
}

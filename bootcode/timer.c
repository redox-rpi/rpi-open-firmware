/*
 * redox-rpi bootcode: busy-wait delay on the 1 MHz system timer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

void udelay(uint32_t us) {
	uint32_t start = timer_us();
	while (timer_us() - start <= us) {
	}
}

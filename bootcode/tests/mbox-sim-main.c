/*
 * Test driver for mbox.c in the resim VC4 simulator (vc4emul), run by
 * mbox-sim.sh once for the release build (MBOX_TRACE=0) and once for the
 * debug build (MBOX_TRACE=1). mbox.c is included, so its static functions
 * can be called: property() answers a request buffer that lies in the
 * simulator's memory, beat() is the "alive" line of mbox_serve().
 *
 * Output goes to resim's mini UART (GPIO 14 alt 5, AUX enabled), the only
 * serial port resim models; printf is the bootcode's own printf.c.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "mbox.c"

#define REQ 0x00800000u /* the ARM's request buffer, 8 MiB into its memory */

void uart_putc(char c) {
	*(volatile uint32_t *)0x7e215040u = (uint8_t)c;
}

void udelay(uint32_t us) {
	(void)us;
}

bool usb_power_on(void) {
	return true;
}

void sd_pullups(void) {
}

uint32_t arm_khz(void) {
	return 1200000;
}

uint32_t arm_max_mhz = 1400;

uint32_t arm_set_mhz(uint32_t mhz) {
	return mhz;
}

int main(void) {
	*(volatile uint32_t *)0x7e200004u = 2u << 12; /* GPFSEL1: GPIO 14 alt 5 */
	*(volatile uint32_t *)0x7e215004u = 1;        /* AUX_ENABLES: mini UART */
	printf("mbox-sim: start, MBOX_TRACE=%u\n", MBOX_TRACE);

	/* board revision (answered) and EDID block 0 (not answered yet) */
	volatile uint32_t *b = (volatile uint32_t *)(ALIAS_UNCACHED | REQ);
	static const uint32_t req[] = {
		0, 0,                   /* size, code */
		0x00010002, 4, 0, 0,    /* board revision */
		0x00030020, 8, 4, 0, 0, /* EDID block 0 */
		0,                      /* end */
	};
	for (unsigned i = 0; i < sizeof req / sizeof req[0]; i++)
		b[i] = req[i];
	b[0] = sizeof req;
	property(REQ);
	printf("RESULT code %08X revision-tag %08X edid-tag %08X\n", b[1], b[4], b[8]);

	/* a buffer that is too small: an error, printed in every build */
	b[0] = 5;
	property(REQ);

	/* the alive line: not before 10 s, then once */
	g_next_beat = 10000000u;
	beat(5000000u);
	beat(10000000u);
	beat(15000000u);

	printf("mbox-sim: end\n");
	return 0;
}

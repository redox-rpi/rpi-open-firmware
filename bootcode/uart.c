/*
 * redox-rpi bootcode: PL011 UART on GPIO 14/15, 115200 8N1.
 *
 * Sequence and values from rpi-open-firmware firmware/romstage.c,
 * uart_init() and uart_putc() (commit 82a6264, GPLv2+). UART clock:
 * oscillator / (0x6666 / 4096 = 6.4) = 3 MHz; 3 MHz / (16 * 115200) =
 * 1.628 = IBRD 1, FBRD 40 (40/64).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define UART_FR   HW_REGISTER_RW(UART_BASE + 0x18)
#define UART_IBRD HW_REGISTER_RW(UART_BASE + 0x24)
#define UART_FBRD HW_REGISTER_RW(UART_BASE + 0x28)
#define UART_LCRH HW_REGISTER_RW(UART_BASE + 0x2c)
#define UART_CR   HW_REGISTER_RW(UART_BASE + 0x30)
#define UART_ICR  HW_REGISTER_RW(UART_BASE + 0x44)

#define UART_FR_RXFE 0x10
#define UART_FR_TXFF 0x20

void uart_init(void) {
	/* GPIO 14 and 15: ALT0 (TXD0, RXD0) */
	uint32_t fsel = GP_FSEL1;
	fsel &= ~((7u << 12) | (7u << 15));
	fsel |= (4u << 12) | (4u << 15);
	GP_FSEL1 = fsel;

	UART_CR = 0;

	/* no pull-up or pull-down on 14/15 */
	GP_PUD = 0;
	udelay(150);
	GP_PUDCLK0 = (1u << 14) | (1u << 15);
	udelay(150);
	GP_PUDCLK0 = 0;

	CM_UARTDIV = CM_PASSWORD | 0x6666;
	CM_UARTCTL = CM_PASSWORD | CM_SRC_OSC | CM_UARTCTL_FRAC_SET | CM_UARTCTL_ENAB_SET;

	UART_ICR = 0x7ff;
	UART_IBRD = 1;
	UART_FBRD = 40;
	UART_LCRH = 0x70;  /* 8 bits, FIFO on */
	UART_CR = 0x301;   /* RX, TX, UART enable */
}

void uart_putc(char c) {
	if (c == '\n')
		uart_putc('\r');
	while (UART_FR & UART_FR_TXFF) {
	}
	UART_RBRTHRDLL = (uint8_t)c;
}

/* -1 if nothing arrives within timeout_us */
int uart_getc_timeout(uint32_t timeout_us) {
	uint32_t t0 = timer_us();
	while (UART_FR & UART_FR_RXFE) {
		if (timer_us() - t0 >= timeout_us)
			return -1;
	}
	return (int)(UART_RBRTHRDLL & 0xff);
}

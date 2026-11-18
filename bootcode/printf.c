/*
 * redox-rpi bootcode: small printf for the UART.
 *
 * Supports %% %c %s %d %i %u %x %X %p, a '0' flag, a field width and an
 * ignored 'l'. New code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

static int put_str(const char *s, int width, char pad) {
	int len = 0;
	for (const char *p = s; *p; p++)
		len++;
	int n = 0;
	for (; width > len; width--, n++)
		uart_putc(pad);
	for (; *s; s++, n++)
		uart_putc(*s);
	return n;
}

static int put_num(uint32_t v, unsigned base, bool upper, bool neg, int width, char pad) {
	char buf[12];
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int i = sizeof(buf) - 1;
	buf[i] = 0;
	do {
		buf[--i] = digits[v % base];
		v /= base;
	} while (v);
	if (neg) {
		if (pad == '0') {
			uart_putc('-');
			return 1 + put_str(&buf[i], width - 1, pad);
		}
		buf[--i] = '-';
	}
	return put_str(&buf[i], width, pad);
}

int vprintf(const char *fmt, va_list ap) {
	int n = 0;
	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			uart_putc(*fmt);
			n++;
			continue;
		}
		fmt++;
		char pad = ' ';
		int width = 0;
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		if (*fmt == 'l')
			fmt++;
		switch (*fmt) {
		case 'c':
			uart_putc((char)va_arg(ap, int));
			n++;
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			n += put_str(s ? s : "(null)", width, ' ');
			break;
		}
		case 'd':
		case 'i': {
			int32_t v = va_arg(ap, int32_t);
			n += put_num(v < 0 ? -(uint32_t)v : (uint32_t)v, 10, false, v < 0, width, pad);
			break;
		}
		case 'u':
			n += put_num(va_arg(ap, uint32_t), 10, false, false, width, pad);
			break;
		case 'x':
		case 'X':
			n += put_num(va_arg(ap, uint32_t), 16, *fmt == 'X', false, width, pad);
			break;
		case 'p':
			n += put_str("0x", 0, ' ');
			n += put_num((uint32_t)va_arg(ap, void *), 16, false, false, 8, '0');
			break;
		case '%':
			uart_putc('%');
			n++;
			break;
		case 0:
			return n;
		default:
			uart_putc('%');
			uart_putc(*fmt);
			n += 2;
		}
	}
	return n;
}

int printf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int n = vprintf(fmt, ap);
	va_end(ap);
	return n;
}

/*
 * redox-rpi bootcode: memset and memcpy, which GCC may emit for struct
 * and array initialisers even with -ffreestanding.
 *
 * New code. SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

void *memset(void *dst, int c, size_t n) {
	volatile uint8_t *d = dst;
	while (n--)
		*d++ = (uint8_t)c;
	return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
	volatile uint8_t *d = dst;
	const volatile uint8_t *s = src;
	while (n--)
		*d++ = *s++;
	return dst;
}

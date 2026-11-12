/*
 * redox-rpi bootcode: bit-banged I2C on GPIO 46/47 (monitor builds), to
 * find the 3B+'s GPIO expander (#230 M3).
 *
 * On the Pi 3B+ the Ethernet chip's reset line LAN_RUN, the power LED, BT_ON
 * and WL_ON are pins 128-134 of a GPIO expander that start.elf drives
 * (Raspberry Pi's dt-blob.dts, pins_3bplus); GPIO 46/47 are the board's
 * SMPS_SCL/SMPS_SDA lines. Until we know the chip, the monitor's i2cscan
 * lists the addresses that answer, and i2cr reads their registers. Reads
 * only. Standard I2C, open drain: a line is pulled low by making the pin an
 * output at 0 and let go by making it an input. About 50 kHz. New code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define I2C_SCL 46
#define I2C_SDA 47
#define I2C_HALF_US 10

static void pin_mode(int pin, uint32_t mode) {
	uint32_t shift = (uint32_t)(pin - 40) * 3;
	GP_FSEL4 = (GP_FSEL4 & ~(7u << shift)) | (mode << shift);
}

static void line_low(int pin) {
	GP_CLR1 = 1u << (pin - 32);
	pin_mode(pin, 1);
}

static void line_release(int pin) { pin_mode(pin, 0); }

static int line_read(int pin) { return (int)((GP_LEV1 >> (pin - 32)) & 1); }

static void half(void) { udelay(I2C_HALF_US); }

/* let SCL go high, waiting while a device stretches the clock */
static bool scl_high(void) {
	line_release(I2C_SCL);
	for (int i = 0; !line_read(I2C_SCL); i++) {
		if (i > 1000)
			return false;
		udelay(1);
	}
	half();
	return true;
}

static void start(void) {
	line_release(I2C_SDA);
	scl_high();
	line_low(I2C_SDA);
	half();
	line_low(I2C_SCL);
	half();
}

static void stop(void) {
	line_low(I2C_SDA);
	half();
	scl_high();
	line_release(I2C_SDA);
	half();
}

/* true when the device acknowledged */
static bool write_byte(uint32_t b) {
	for (int i = 7; i >= 0; i--) {
		if ((b >> i) & 1)
			line_release(I2C_SDA);
		else
			line_low(I2C_SDA);
		half();
		if (!scl_high())
			return false;
		line_low(I2C_SCL);
	}
	line_release(I2C_SDA);
	half();
	if (!scl_high())
		return false;
	bool ack = !line_read(I2C_SDA);
	line_low(I2C_SCL);
	half();
	return ack;
}

static uint32_t read_byte(bool ack) {
	uint32_t b = 0;
	line_release(I2C_SDA);
	for (int i = 0; i < 8; i++) {
		half();
		scl_high();
		b = (b << 1) | (uint32_t)line_read(I2C_SDA);
		line_low(I2C_SCL);
	}
	if (ack)
		line_low(I2C_SDA);
	half();
	scl_high();
	line_low(I2C_SCL);
	line_release(I2C_SDA);
	half();
	return b;
}

static uint32_t g_fsel4;

static void bus_open(void) {
	g_fsel4 = GP_FSEL4;
	line_release(I2C_SCL);
	line_release(I2C_SDA);
	half();
}

/* leave 46/47 as we found them */
static void bus_close(void) { GP_FSEL4 = g_fsel4; }

void i2c_scan(void) {
	bus_open();
	printf("i2c: GPIO 46/47 idle levels SCL %d SDA %d\n", line_read(I2C_SCL), line_read(I2C_SDA));
	printf("i2c: answering:");
	int n = 0;
	for (uint32_t a = 0x08; a < 0x78; a++) {
		start();
		bool ack = write_byte(a << 1);
		stop();
		if (ack) {
			printf(" %02X", a);
			n++;
		}
	}
	printf("\n");
	bus_close();
	printf("ok i2cscan %d\n", n);
}

void i2c_read(uint32_t addr, uint32_t reg, uint32_t n) {
	if (addr > 0x7f || reg > 0xff || n == 0 || n > 32) {
		printf("err usage: i2cr ADDR REG N (N 1..32)\n");
		return;
	}
	bus_open();
	start();
	bool ok = write_byte(addr << 1) && write_byte(reg);
	if (ok) {
		start(); /* repeated start */
		ok = write_byte((addr << 1) | 1);
	}
	if (!ok) {
		stop();
		bus_close();
		printf("err i2c: no answer from %02X\n", addr);
		return;
	}
	printf("i2c %02X @%02X:", addr, reg);
	for (uint32_t i = 0; i < n; i++)
		printf(" %02X", read_byte(i + 1 < n));
	stop();
	bus_close();
	printf("\nok\n");
}

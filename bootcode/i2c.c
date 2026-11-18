/*
 * redox-rpi bootcode: bit-banged I2C on GPIO 46/47 (monitor builds), to
 * find and drive the 3B+'s GPIO expander (#230 M3).
 *
 * On the Pi 3B+ the Ethernet chip's reset line LAN_RUN, the power LED, BT_ON
 * and WL_ON are pins 128-134 of a GPIO expander that start.elf drives
 * (Raspberry Pi's dt-blob.dts, pins_3bplus); GPIO 46/47 are the board's
 * SMPS lines (clock and data, order below). Until we know the chip, the monitor's i2cscan
 * lists the addresses that answer, i2cr reads their registers and i2cw
 * writes one. Standard I2C, open drain: a line is pulled low by making the pin an
 * output at 0 and let go by making it an input. About 50 kHz. New code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

/*
 * Which of 46/47 is the clock is not clear: the pin comments in dt-blob.dts
 * say 46 SMPS_SCL, its pin_defines (what start.elf uses) say SMPS_SDA 46 and
 * SMPS_SCL 47. Row 366 found nothing with SCL on 46, so i2cscan tries both
 * and keeps the order where a device answers.
 */
static int I2C_SCL = 47;
static int I2C_SDA = 46;
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

static int scan_once(void) {
	bus_open();
	printf("i2c: SCL GPIO %d, SDA GPIO %d, idle levels SCL %d SDA %d\n", I2C_SCL, I2C_SDA,
	       line_read(I2C_SCL), line_read(I2C_SDA));
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
	return n;
}

void i2c_scan(void) {
	int n = scan_once();
	if (n == 0) {
		int t = I2C_SCL;
		I2C_SCL = I2C_SDA;
		I2C_SDA = t;
		n = scan_once();
		if (n == 0) { /* back to the pin_defines' order */
			I2C_SCL = 47;
			I2C_SDA = 46;
		}
	}
	printf("ok i2cscan %d SCL %d SDA %d\n", n, I2C_SCL, I2C_SDA);
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

static bool i2c_write_reg(uint32_t addr, uint32_t reg, uint32_t value) {
	bus_open();
	start();
	bool ok = write_byte(addr << 1) && write_byte(reg) && write_byte(value);
	stop();
	bus_close();
	return ok;
}

static bool i2c_read_reg(uint32_t addr, uint32_t reg, uint32_t *value) {
	bus_open();
	start();
	bool ok = write_byte(addr << 1) && write_byte(reg);
	if (ok) {
		start();
		ok = write_byte((addr << 1) | 1);
	}
	if (ok)
		*value = read_byte(false);
	stop();
	bus_close();
	return ok;
}

void i2c_write(uint32_t addr, uint32_t reg, uint32_t value) {
	if (addr > 0x7f || reg > 0xff || value > 0xff) {
		printf("err usage: i2cw ADDR REG VALUE\n");
		return;
	}
	if (i2c_write_reg(addr, reg, value))
		printf("ok i2cw %02X @%02X = %02X\n", addr, reg, value);
	else
		printf("err i2c: no answer from %02X\n", addr);
}

/*
 * The 3B+'s GPIO expander: an FXL6408 at 0x43 on the SMPS bus (Pi test row
 * 367: register 1 reads A2, manufacturer 101 = Fairchild, part 000 =
 * FXL6408). Its pins after power-on are inputs, outputs high-Z, pulled down.
 * dt-blob.dts pins_3bplus: pin 0 BT_ON, 1 WL_ON, 2 PWR_LED (active low,
 * starts on), 3 LAN_RUN (starts on), 5 camera shutdown, 6 camera LED.
 * Registers from the FXL6408 datasheet: 0x03 direction (1 = output),
 * 0x05 output state, 0x07 output high-Z (1 = high-Z).
 */
#define EXP_ADDR 0x43
#define EXP_DIR 0x03
#define EXP_OUT 0x05
#define EXP_HIZ 0x07
#define EXP_PWR_LED (1u << 2)
#define EXP_LAN_RUN (1u << 3)

/* LAN_RUN high (the LAN7515 leaves reset), power LED on, as start.elf does */
bool expander_start(void) {
	uint32_t id = 0;
	if (!i2c_read_reg(EXP_ADDR, 0x01, &id) || (id & 0xfc) != 0xa0) {
		printf("err expander: no FXL6408 at 43 (id %02X)\n", id);
		return false;
	}
	uint32_t pins = EXP_PWR_LED | EXP_LAN_RUN;
	bool ok = i2c_write_reg(EXP_ADDR, EXP_OUT, EXP_LAN_RUN) /* PWR_LED low = on */
		&& i2c_write_reg(EXP_ADDR, EXP_DIR, pins)
		&& i2c_write_reg(EXP_ADDR, EXP_HIZ, 0xffu & ~pins);
	uint32_t dir = 0, out = 0, hiz = 0;
	i2c_read_reg(EXP_ADDR, EXP_DIR, &dir);
	i2c_read_reg(EXP_ADDR, EXP_OUT, &out);
	i2c_read_reg(EXP_ADDR, EXP_HIZ, &hiz);
	printf("expander: FXL6408 id %02X, LAN_RUN on, dir %02X out %02X hiz %02X%s\n", id, dir, out,
	       hiz, ok ? "" : " (a write failed)");
	return ok;
}

/*
 * Register comparison for #230 M3 (monitor `regs`): every clock, PLL and
 * GPIO register that Pi test 337 dumped under start.elf, with start.elf's
 * value, next to ours now. USB core registers only when PM_USB is on.
 */
struct reg_ref {
	uint32_t addr, ref;
	const char *name;
};
#include "regs337.h"

void regs_compare(void) {
	int diff = 0;
	for (uint32_t i = 0; i < sizeof g_regs / sizeof g_regs[0]; i++) {
		const struct reg_ref *r = &g_regs[i];
		if ((r->addr >> 12) == 0x7e980 || (r->addr >> 12) == 0x7e981) {
			if (!(PM_USB & 1))
				continue;
		}
		uint32_t v = HW_REGISTER_RW(r->addr);
		bool same = r->ref == 0xffffffffu || v == r->ref;
		if (!same)
			diff++;
		printf("reg %08X %08X %08X %s%s\n", r->addr, v, r->ref, r->name, same ? "" : " DIFF");
	}
	printf("ok regs %d differ\n", diff);
}

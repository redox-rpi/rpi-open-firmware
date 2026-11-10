/*
 * redox-rpi bootcode: common header.
 *
 * Register names come from Broadcom's released VideoCore IV headers
 * (../firmware/broadcom, BSD licence), through rpi-open-firmware's
 * ../firmware/hardware.h glue (GPLv2+).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware.h"

/* System timer, free running at 1 MHz once clock_init() has set CM_TIMER. */
#define ST_CLO_REG HW_REGISTER_RO(0x7e003004)

/* bus address aliases of the same SDRAM: 0x8... L2 cached, 0xc... uncached */
#define ALIAS_UNCACHED 0xc0000000u

/* main.c */
extern uint32_t g_cpuid;
void panic(const char *fmt, ...) __attribute__((noreturn));

/* clock.c */
void clock_init(void);

/* timer.c */
static inline uint32_t timer_us(void) { return ST_CLO_REG; }
void udelay(uint32_t us);

/* uart.c */
void uart_init(void);
void uart_putc(char c);
int uart_getc_timeout(uint32_t timeout_us);

/* printf.c: no format attribute, because uint32_t is long on VC4 while
 * int and long are both 32 bits; our printf reads every number as 32 bits */
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);

/* sdram.c */
enum ram_size { RAM_1GB, RAM_512MB, RAM_256MB, RAM_128MB, RAM_UNKNOWN };
extern enum ram_size g_ram_size;
void sdram_init(void);

/* monitor.c: test builds only (MONITOR=1), never in a release;
 * CHAIN_IMAGE=1 for the boot selector build (Makefile CHAIN_IMAGE=path) */
#ifndef CHAIN_IMAGE
#define CHAIN_IMAGE 0
#endif
void monitor(uint32_t wait_s);
void monitor_chain_builtin(void) __attribute__((noreturn));

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
extern uint32_t g_rom_sdcs;  /* SD_CS as the Boot ROM left it */
extern bool g_sdram_up;      /* sdram_init() has run */
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
bool monitor_wait(uint32_t wait_s);
void monitor_run(void);
void monitor_chain_builtin(void) __attribute__((noreturn));
void watchdog_reset(void) __attribute__((noreturn));

/* arm.c (monitor builds): start the ARM cores at ARM address 0 */
bool arm_start(bool aarch64);

/* sd.c, fat.c (monitor builds): read the SD card and its FAT partition */
struct sd_card { bool ready, high_capacity; uint32_t mb; char name[6]; };
extern struct sd_card g_sd;
bool sd_init(void);
bool sd_read(uint32_t lba, uint32_t *buf);
void sd_off(void);
bool fat_mount(void);
bool fat_list(void);
int32_t fat_load(const char *name, uint32_t addr, uint32_t max);
bool fdt_set_memory(uint8_t *dtb, uint32_t len, uint32_t base, uint32_t size);

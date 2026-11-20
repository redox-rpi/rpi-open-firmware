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
/* memory split (#29): the ARM gets 0 .. g_arm_mem, the VideoCore the
 * last VC_MEM bytes of the SDRAM (boot screen panel, the ARM's
 * framebuffer). sdram_init() sets both from the size it detects; until
 * then (and in the resim tests) they are the 1 GB values. */
#define VC_MEM 0x2000000u /* 32 MB */
extern uint32_t g_ram_bytes, g_arm_mem;
void sdram_init(void);
void sdram_size_kept(void); /* SDRAM already up: the size from SD_SB */

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
extern uint32_t arm_max_mhz;
uint32_t arm_set_mhz(uint32_t mhz);
uint32_t arm_khz(void);

/* sd.c, fat.c (monitor builds): read the SD card and its FAT partition */
struct sd_card { bool ready, high_capacity, wide; uint32_t mb; char name[6]; };
#define SD_RUN_MAX 2048u /* sectors per read command (1 MB) */
extern struct sd_card g_sd;
bool sd_init(void);
bool sd_read(uint32_t lba, uint32_t *buf);
bool sd_read_blocks(uint32_t lba, uint32_t n, uint32_t *buf);
void sd_off(void);
bool fat_mount(void);
bool fat_list(void);
int32_t fat_load(const char *name, uint32_t addr, uint32_t max);
int32_t fat_size(const char *name);
/* called by fat_load after each cluster when set (bytes done, file size) */
extern void (*g_fat_progress)(uint32_t done, uint32_t total);

/* dt.c: edit a devicetree in a buffer of cap bytes (offsets name nodes;
 * a change that grows the blob makes later offsets stale) */
struct dt { uint8_t *b; uint32_t cap; };
bool dt_open(struct dt *t, uint8_t *b, uint32_t len, uint32_t cap);
uint32_t dt_size(const struct dt *t);
void dt_finish(struct dt *t);
int32_t dt_root(const struct dt *t);
int32_t dt_path(const struct dt *t, const char *path);
int32_t dt_subnode(const struct dt *t, int32_t node, const char *name);
int32_t dt_add_subnode(struct dt *t, int32_t node, const char *name);
int32_t dt_by_phandle(const struct dt *t, uint32_t ph);
const uint8_t *dt_prop(const struct dt *t, int32_t node, const char *name, uint32_t *len);
bool dt_setprop(struct dt *t, int32_t node, const char *name, const void *val, uint32_t len);
bool dt_setprop_u32(struct dt *t, int32_t node, const char *name, uint32_t v);
bool dt_setprop_str(struct dt *t, int32_t node, const char *name, const char *s);
bool dt_apply_overlay(struct dt *base, struct dt *overlay, const char *name);
bool dt_set_memory(struct dt *t, uint32_t base, uint32_t size);
bool dt_set_chosen(struct dt *t, const char *prop, const char *str, uint32_t n);
bool dt_set_chosen_u32(struct dt *t, const char *prop, uint32_t v);

/* config.c: the config.txt keys of a direct boot (rof #48) */
#define CFG_OVERLAYS 8
struct boot_cfg {
	bool arm_64bit, device_tree_set;
	char kernel[32], initramfs[32], armstub[32], device_tree[40], cmdline[32];
	char overlay[CFG_OVERLAYS][32];
	uint32_t n_overlays, unknown_filters;
	uint32_t diag_period; /* diag_period=US: start the sampler at boot (#66) */
	bool diag_axi;        /* diag_axi=1: with AXI bus counters */
};
void cfg_defaults(struct boot_cfg *c);
void cfg_parse(struct boot_cfg *c, const char *txt, uint32_t len, uint32_t board_type, uint32_t lev0, uint32_t lev1);
const char *cfg_board_dtb(uint32_t board_type);

/* boot64.c (monitor and release builds): load from FAT and start the ARM;
 * returns only on an error */
bool boot64(const char *kernel, const char *dtb, uint32_t dtb_in_mem);
uint32_t crc32(const volatile uint8_t *p, uint32_t n);

/* mbox.c (monitor builds): answer the ARM's mailbox property requests once
 * the ARM runs; mbox_poll() is called from uart_getc_timeout()'s wait loop.
 * The ARM gets 0 .. MBOX_ARM_MEM_SIZE (g_arm_mem: 62 x 16 MB on a 1 GB
 * board, 30 x 16 MB on 512 MB, as arm_start() maps it). */
#define MBOX_ARM_MEM_SIZE g_arm_mem
extern bool g_mbox_on;
void mbox_start(void);
uint32_t board_revision(void); /* OTP row 30, e.g. a020d3 (3B+), 902120 (Zero 2 W) */
#define BOARD_TYPE_3BP 0x0du /* board type, bits 4..11 of the revision */
void mbox_poll(void);
/* diag.c: display/graphics sampler into a ring buffer for the ARM (#66) */
#define DIAG_AXI 0x80000000u /* with diag_start's period: AXI bus counters too */
void diag_start(uint32_t period_us); /* 0 stops */
void diag_poll(void);
void diag_dump(uint32_t count);
uint32_t diag_bus_addr(void);
uint32_t diag_bytes(void);
void mbox_serve(void) __attribute__((noreturn));
/* monitor: ask for a framebuffer through the real tag path, as U-Boot does */
bool mbox_fb_test(uint32_t w, uint32_t h, uint32_t depth, uint32_t order, uint32_t *base, uint32_t *pitch);

/* power.c (monitor builds): image and USB power domains, USB PHY */
bool usb_power_on(void);
bool image_domain_on(void);

/* the ARM's framebuffer as the mailbox describes it (mbox.c g_fb) */
struct fb_view {
	uint32_t base; /* bus address */
	uint32_t w, h, vw, vh, depth, order, xoff, yoff; /* order: 0 BGR, 1 RGB */
};

/* hvs.c */
#define HVS_PLANE_WORDS 7u
uint32_t hvs_plane(uint32_t *dl, const struct fb_view *v, uint32_t scr_w, uint32_t scr_h);
uint32_t fb_pixel(uint32_t rgb, uint32_t depth, uint32_t order);
uint32_t fb_pattern(volatile uint8_t *fb, uint32_t w, uint32_t h, uint32_t depth, uint32_t order, uint32_t pitch);

/* hdmi.c (HDMI=1 builds): 1080p60 DVI, one colour (#442 step 1) or the
 * ARM's framebuffer (step 2); g_hdmi_log: the step lines on the UART
 * (on in test builds, off in a release, where every line costs time) */
#ifndef HDMI
#define HDMI 0
#endif
extern bool g_hdmi_log;
bool hdmi_on(uint32_t rgb);
void hdmi_fb(const struct fb_view *v); /* show it (v == NULL: background only) */
void hdmi_status(void);

/* bootscreen.c (HDMI=1 builds): title, progress bar and boot steps on HDMI
 * until the ARM shows its own framebuffer (#430). The panel lies at the
 * start of the VideoCore's memory, below the ARM's framebuffer (mbox.c). */
#define BS_BASE MBOX_ARM_MEM_SIZE
#define BS_W 640u
#define BS_H 160u
#if HDMI
bool bs_start(const char *a, const char *b, uint32_t pct);
void bs_step(uint32_t pct, const char *a, const char *b);
void bs_fail(const char *a, const char *b);
void bs_handover(const struct fb_view *v);
void bs_title(const char *s);
void bs_update(uint32_t pct, const char *a, const char *b);
bool bs_config_title(const char *txt, uint32_t len);
bool bs_demo(void);
void bs_report(void);
#else
#define bs_start(a, b, pct) ((void)(a), (void)(b), (void)(pct))
#define bs_step(pct, a, b) ((void)(pct), (void)(a), (void)(b))
#define bs_fail(a, b) ((void)(a), (void)(b))
#define bs_update(pct, a, b) ((void)(pct), (void)(a), (void)(b))
#define bs_handover(v) ((void)(v))
#define bs_config_title(t, n) ((void)(t), (void)(n), false)
#endif

/* i2c.c (monitor builds): bit-banged I2C on GPIO 46/47, reads only */
void i2c_scan(void);
void i2c_read(uint32_t addr, uint32_t reg, uint32_t n);
void i2c_write(uint32_t addr, uint32_t reg, uint32_t value);
bool expander_start(void);
void pllc_per_on(void);
uint32_t core_khz(void);
uint32_t pllc_per_khz(void);
void eth_clock_on(void);
void regs_compare(void);
void sd_pullups(void);
void vpu_clock(uint32_t div);
void sd_clock_update(void);
void lan_reset_pulse_begin(void);
void lan_reset_pulse_end(void);

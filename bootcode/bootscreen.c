/*
 * redox-rpi bootcode: the boot screen on HDMI (#430). From the moment the
 * SDRAM runs until the ARM asks for its own framebuffer, a small panel in
 * the middle of the 1080p screen shows a title, a progress bar and the
 * last few boot steps. It is compiled in, so it needs nothing from the SD
 * card; the font is U-Boot's 8x16 (font8x16.h).
 *
 * The panel is a 640x160 RGB565 buffer in the VideoCore's part of the
 * SDRAM, shown as one HVS plane (hdmi_fb(), hvs.c); the rest of the screen
 * is the black background fill. A step redraws only the bar and the text
 * lines. When the ARM allocates a framebuffer (U-Boot, later Redox), mbox.c
 * shows that one instead and calls bs_handover(); from then on the steps
 * draw nothing.
 *
 * Drawing touches only memory, so tests/bootscreen-sim.sh runs it in resim
 * and compares the panel with a Python model. New code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include "font8x16.h"

#define BS_TITLE "Redox on Raspberry Pi"
#define BS_PITCH (BS_W * 2u)
#define BS_TITLE_Y 0u /* twice the font size: 32 lines */
#define BS_BAR_Y 48u
#define BS_BAR_H 16u
#define BS_BORDER 2u
#define BS_LINE_Y 80u
#define BS_LINE_STEP 20u
#define BS_LINES 4u
#define BS_LINE_MAX (BS_W / FONT_W)

/* RGB565 */
#define C_BLACK 0x0000u  /* 000000, as the HVS background */
#define C_WHITE 0xffffu  /* ffffff: title, the newest step */
#define C_GREY 0xa514u   /* a0a0a0: older steps */
#define C_RED 0xfa08u    /* ff4040: a step that failed */
#define C_BORDER 0x8410u /* 808080 */
#define C_EMPTY 0x3186u  /* 303030: the bar still to go */
#define C_BAR 0xfc00u    /* ff8000 */

static char g_line[BS_LINES][BS_LINE_MAX + 1];
static bool g_fail[BS_LINES];
static uint32_t g_nline, g_pct;
static bool g_on;
static uint32_t g_t_up, g_t_hdmi, g_t_draw;

/* the panel's memory; tests/bootscreen-sim.sh points it elsewhere */
volatile uint8_t *g_bs_fb = (volatile uint8_t *)(ALIAS_UNCACHED | BS_BASE);

/* x and w even: two pixels per word */
static void fill(volatile uint8_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) {
	uint32_t c2 = c | c << 16;
	for (uint32_t j = 0; j < h; j++) {
		volatile uint32_t *p = (volatile uint32_t *)(fb + (y + j) * BS_PITCH + x * 2);
		for (uint32_t i = 0; i < w / 2; i++)
			p[i] = c2;
	}
}

/* string s at x, y, the font scaled by 1 << sh; the rest of each line up
 * to x_end in bg, so a shorter text covers a longer one */
static void text(volatile uint8_t *fb, uint32_t x, uint32_t y, const char *s, uint32_t sh, uint32_t fg, uint32_t bg,
		 uint32_t x_end) {
	uint32_t cw = FONT_W << sh;
	for (uint32_t row = 0; row < FONT_H << sh; row++) {
		volatile uint16_t *p = (volatile uint16_t *)(fb + (y + row) * BS_PITCH);
		uint32_t px = x;
		for (const char *c = s; *c && px + cw <= x_end; c++) {
			uint32_t ch = (uint8_t)*c;
			uint32_t bits = ch >= FONT_FIRST && ch <= FONT_LAST ? g_font[(ch - FONT_FIRST) * FONT_H + (row >> sh)] : 0;
			for (uint32_t b = 0; b < cw; b++)
				p[px++] = bits & (0x80u >> (b >> sh)) ? fg : bg;
		}
		while (px < x_end)
			p[px++] = bg;
	}
}

static void draw_bar(volatile uint8_t *fb) {
	uint32_t inner = BS_W - 2 * BS_BORDER;
	uint32_t done = (inner * g_pct / 100) & ~1u;
	fill(fb, 0, BS_BAR_Y, BS_W, BS_BORDER, C_BORDER);
	fill(fb, 0, BS_BAR_Y + BS_BAR_H - BS_BORDER, BS_W, BS_BORDER, C_BORDER);
	fill(fb, 0, BS_BAR_Y + BS_BORDER, BS_BORDER, BS_BAR_H - 2 * BS_BORDER, C_BORDER);
	fill(fb, BS_W - BS_BORDER, BS_BAR_Y + BS_BORDER, BS_BORDER, BS_BAR_H - 2 * BS_BORDER, C_BORDER);
	fill(fb, BS_BORDER, BS_BAR_Y + BS_BORDER, done, BS_BAR_H - 2 * BS_BORDER, C_BAR);
	fill(fb, BS_BORDER + done, BS_BAR_Y + BS_BORDER, inner - done, BS_BAR_H - 2 * BS_BORDER, C_EMPTY);
}

/* the newest step at the bottom in white, the older ones above in grey */
static void draw_lines(volatile uint8_t *fb) {
	for (uint32_t i = 0; i < BS_LINES; i++) {
		uint32_t fg = g_fail[i] ? C_RED : i == g_nline - 1 ? C_WHITE : C_GREY;
		text(fb, 0, BS_LINE_Y + i * BS_LINE_STEP, i < g_nline ? g_line[i] : "", 0, fg, C_BLACK, BS_W);
	}
}

static void draw_all(volatile uint8_t *fb) {
	fill(fb, 0, 0, BS_W, BS_H, C_BLACK);
	uint32_t tw = (sizeof BS_TITLE - 1) * FONT_W * 2;
	text(fb, (BS_W - tw) / 2, BS_TITLE_Y, BS_TITLE, 1, C_WHITE, C_BLACK, (BS_W + tw) / 2);
	draw_bar(fb);
	draw_lines(fb);
}

/* a + b as the newest line; the oldest goes when all lines are used */
static void add_line(const char *a, const char *b, bool fail) {
	if (g_nline == BS_LINES) {
		for (uint32_t i = 1; i < BS_LINES; i++) {
			for (uint32_t k = 0; k <= BS_LINE_MAX; k++)
				g_line[i - 1][k] = g_line[i][k];
			g_fail[i - 1] = g_fail[i];
		}
		g_nline--;
	}
	char *d = g_line[g_nline];
	uint32_t n = 0;
	for (; a && *a && n < BS_LINE_MAX; a++)
		d[n++] = *a;
	for (; b && *b && n < BS_LINE_MAX; b++)
		d[n++] = *b;
	d[n] = 0;
	g_fail[g_nline++] = fail;
}

/*
 * Start HDMI and show the panel with its first line a + b (b may be NULL)
 * and the bar at pct percent. False when HDMI did not come up; the steps
 * then draw nothing and the boot goes on as without a screen.
 */
bool bs_start(const char *a, const char *b, uint32_t pct) {
	uint32_t t0 = timer_us();
	g_on = false;
	g_nline = 0;
	g_pct = pct;
	add_line(a, b, false);
	if (!hdmi_on(0x000000)) {
		printf("bootscreen: HDMI did not come up, no boot screen\n");
		return false;
	}
	uint32_t t1 = timer_us();
	draw_all(g_bs_fb);
	struct fb_view v = {(uint32_t)g_bs_fb, BS_W, BS_H, BS_W, BS_H, 16, 1, 0, 0};
	hdmi_fb(&v);
	g_on = true;
	g_t_up = timer_us();
	g_t_hdmi = t1 - t0;
	g_t_draw = g_t_up - t1;
	return true;
}

static void step(uint32_t pct, const char *a, const char *b, bool fail) {
	if (!g_on)
		return;
	uint32_t t0 = timer_us();
	add_line(a, b, fail);
	if (pct > g_pct) /* the bar never goes back */
		g_pct = pct;
	draw_bar(g_bs_fb);
	draw_lines(g_bs_fb);
	g_t_draw += timer_us() - t0;
}

/* a boot step a + b done or begun, the bar to pct percent */
void bs_step(uint32_t pct, const char *a, const char *b) {
	step(pct, a, b, false);
}

/* a step that failed, in red; the bar stays */
void bs_fail(const char *a, const char *b) {
	step(0, a, b, true);
}

/* the ARM shows its own framebuffer now (mbox.c): stop drawing */
void bs_handover(void) {
	if (!g_on)
		return;
	g_on = false;
	printf("bootscreen: handed over to the ARM's framebuffer %u ms after it came up; HDMI start %u us, drawing %u us\n",
	       (timer_us() - g_t_up) / 1000, g_t_hdmi, g_t_draw);
}

/*
 * The steps of a release boot with fixed texts, for the monitor's bs
 * command and the resim test: the panel then has a known content.
 */
bool bs_demo(void) {
	if (!bs_start("bootcode test", ", SDRAM ready", 10))
		return false;
	bs_step(20, "SD card ", "SC64G");
	bs_step(30, "loading ", "armstub8-redox.bin");
	bs_step(45, "loading ", "kernel8.img");
	bs_step(60, "loading ", "redox-3bp.dtb");
	bs_step(75, "starting the ARM", 0);
	bs_fail("a failed step looks ", "like this");
	bs_step(90, "ARM running", 0);
	return true;
}

#if MONITOR
/* monitor: the panel's place, CRC-32 and the time HDMI and drawing took */
void bs_report(void) {
	printf("ok bs %ux%u 16 bit at %08X, crc32 %08X, HDMI start %u us, drawing %u us\n", BS_W, BS_H,
	       (uint32_t)g_bs_fb, crc32(g_bs_fb, BS_PITCH * BS_H), g_t_hdmi, g_t_draw);
}
#endif

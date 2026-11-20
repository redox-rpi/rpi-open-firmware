/*
 * redox-rpi bootcode: the boot screen on HDMI (#430). From the moment the
 * SDRAM runs until the ARM asks for its own framebuffer, a small panel in
 * the middle of the 1080p screen shows a title, a progress bar and the
 * last few boot steps. The title is the firmware's neutral one until
 * boot64 finds boot_title= in the card's config.txt (bs_config_title()),
 * so the system on the card names itself. The panel is compiled in, so it needs nothing from the SD
 * card; the font is U-Boot's 8x16 (font8x16.h).
 *
 * The panel is a 640x160 RGB565 buffer in the VideoCore's part of the
 * SDRAM, shown as one HVS plane (hdmi_fb(), hvs.c); the rest of the screen
 * is the black background fill. A step redraws only the bar and the text
 * lines. When the ARM allocates a framebuffer (U-Boot, later Redox), mbox.c
 * shows that one instead and calls bs_handover(); from then on the steps
 * draw nothing. The handover clears the ARM's framebuffer to black and
 * copies the panel into its middle, so the picture stays until the system
 * draws its own (quiet boot step I4A; until then the buffer held whatever
 * the SDRAM had).
 *
 * Redox's own steps (quiet boot I4B, #43): mailbox tag 0x000f0002 brings
 * a percentage of Redox's boot, flags and a line of text. Redox sends them
 * only with QUIET=1, when nothing else draws into its framebuffer, and the
 * last one before its first console. bs_redox_step() draws them into the
 * panel and copies the changed rows into the ARM's framebuffer, where the
 * handover put the panel; Redox's 0..100 % fill the rest of the bar. If
 * the ARM changes its framebuffer while Redox reports, the panel goes
 * along. After the last step nothing more is drawn.
 *
 * Drawing touches only memory, so tests/bootscreen-sim.sh runs it in resim
 * and compares the panel with a Python model. New code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"
#include "font8x16.h"

#define BS_TITLE "Booting Raspberry Pi ..."
#define BS_TITLE_MAX (BS_W / (FONT_W * 2)) /* 40 characters at twice the size */
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

static char g_title[BS_TITLE_MAX + 1] = BS_TITLE;
static char g_line[BS_LINES][BS_LINE_MAX + 1];
static bool g_fail[BS_LINES];
static uint32_t g_nline, g_pct;
static bool g_on;
static uint32_t g_t_up, g_t_hdmi, g_t_draw;

/* after the handover (#43): the panel in the ARM's framebuffer g_view */
static bool g_arm, g_redox, g_done;
static struct fb_view g_view;
static uint32_t g_base_pct, g_redox_n, g_redox_us;

/* the panel's memory, at the start of the VideoCore's part (BS_BASE, set
 * by bs_start() once the SDRAM size is known); tests/bootscreen-sim.sh
 * points it elsewhere first */
volatile uint8_t *g_bs_fb;

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

/* the title centred (x even); the rest of its 32 lines black */
static void draw_title(volatile uint8_t *fb) {
	uint32_t n = 0;
	while (g_title[n])
		n++;
	uint32_t tw = n * FONT_W * 2, x = (BS_W - tw) / 2 & ~1u;
	fill(fb, 0, BS_TITLE_Y, BS_W, FONT_H * 2, C_BLACK);
	text(fb, x, BS_TITLE_Y, g_title, 1, C_WHITE, C_BLACK, x + tw);
}

static void draw_all(volatile uint8_t *fb) {
	fill(fb, 0, 0, BS_W, BS_H, C_BLACK);
	draw_title(fb);
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
	if (!g_bs_fb)
		g_bs_fb = (volatile uint8_t *)(ALIAS_UNCACHED | BS_BASE);
	g_on = false;
	g_arm = g_redox = g_done = false;
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

/* a new title (printable ASCII, cut at BS_TITLE_MAX), drawn at once */
void bs_title(const char *s) {
	uint32_t n = 0;
	for (; s[n] && n < BS_TITLE_MAX; n++)
		g_title[n] = s[n] >= ' ' && s[n] <= '~' ? s[n] : ' ';
	g_title[n] = 0;
	if (g_on)
		draw_title(g_bs_fb);
}

/*
 * config.txt (len bytes at txt): the value of boot_title= becomes the
 * title. Lines starting with # are comments; spaces around the key, the
 * = and the value are dropped. Broadcom's start.elf ignores the key.
 * True when the key was there.
 */
bool bs_config_title(const char *txt, uint32_t len) {
	static const char key[] = "boot_title";
	const char *p = txt, *end = txt + len;
	while (p < end) {
		while (p < end && (*p == ' ' || *p == '\t'))
			p++;
		const char *k = key;
		while (p < end && *k && *p == *k)
			p++, k++;
		const char *q = p;
		while (q < end && (*q == ' ' || *q == '\t'))
			q++;
		if (!*k && q < end && *q == '=') {
			q++;
			while (q < end && (*q == ' ' || *q == '\t'))
				q++;
			char v[BS_TITLE_MAX + 1];
			uint32_t n = 0;
			while (q < end && *q != '\r' && *q != '\n' && n < BS_TITLE_MAX)
				v[n++] = *q++;
			while (n && (v[n - 1] == ' ' || v[n - 1] == '\t'))
				n--;
			v[n] = 0;
			if (n) {
				bs_title(v);
				return true;
			}
		}
		while (p < end && *p != '\n')
			p++;
		p++;
	}
	return false;
}

/* a boot step a + b done or begun, the bar to pct percent */
void bs_step(uint32_t pct, const char *a, const char *b) {
	step(pct, a, b, false);
}

/* the newest line becomes a + b (a step that goes on, as a large load) */
void bs_update(uint32_t pct, const char *a, const char *b) {
	if (g_on && g_nline && !g_fail[g_nline - 1])
		g_nline--;
	step(pct, a, b, false);
}

/* a step that failed, in red; the bar stays */
void bs_fail(const char *a, const char *b) {
	step(0, a, b, true);
}

/* an RGB565 panel pixel in the ARM framebuffer's format (16 or 32 bit) */
static uint32_t panel_pixel(uint32_t p, uint32_t depth, uint32_t order) {
	uint32_t r = p >> 11 & 31, g = p >> 5 & 63, b = p & 31;
	uint32_t rgb = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
	return fb_pixel(rgb, depth, order);
}

/*
 * Clear n bytes at bus address dst (16-byte aligned, n a multiple of 16)
 * with DMA channel 0: one control block, a 16-byte zero source that does
 * not move, 128-bit writes in bursts. The VPU's own uncached stores took
 * 79 ms for 1280x720x32 (Pi test 466); the DMA engine writes in bursts.
 * The block and the zero source lie in the VideoCore's memory behind the
 * panel (the bootcode itself may run from L2, which the DMA engine does
 * not see). The channel is reset afterwards, before the ARM gets its
 * answer. False when it did not finish (resim has no DMA): the caller
 * then clears with the VPU.
 */
#define BSDMA_CS (*(volatile uint32_t *)0x7e007000u)
#define BSDMA_CONBLK (*(volatile uint32_t *)0x7e007004u)
#define BSDMA_ENABLE (*(volatile uint32_t *)0x7e007ff0u)
#define BSDMA_CS_RESET (1u << 31)
#define BSDMA_CS_END (1u << 1)
#define BSDMA_CS_ACTIVE 1u
#define BSDMA_TI_DEST_INC (1u << 4)
#define BSDMA_TI_DEST_W128 (1u << 5)
#define BSDMA_TI_SRC_W128 (1u << 9)
#define BSDMA_TI_BURST(n) ((uint32_t)(n) << 12)
#define BS_DMA_OFF 0x40000u /* behind the 200 KB panel */

static bool dma_clear(uint32_t dst, uint32_t n) {
	if ((dst | n) & 15 || !n || n >= 1u << 30)
		return false;
	uint32_t blk = BS_BASE + BS_DMA_OFF;
	volatile uint32_t *cb = (volatile uint32_t *)(ALIAS_UNCACHED | blk);
	for (uint32_t i = 8; i < 12; i++)
		cb[i] = 0; /* the zero source, right after the 32-byte block */
	cb[0] = BSDMA_TI_DEST_INC | BSDMA_TI_DEST_W128 | BSDMA_TI_SRC_W128 | BSDMA_TI_BURST(8);
	cb[1] = ALIAS_UNCACHED | (blk + 32);
	cb[2] = dst;
	cb[3] = n;
	cb[4] = 0;
	cb[5] = 0;
	cb[6] = 0;
	cb[7] = 0;
	BSDMA_ENABLE |= 1u;
	BSDMA_CS = BSDMA_CS_RESET;
	BSDMA_CS = BSDMA_CS_END; /* clear a stale END */
	BSDMA_CONBLK = ALIAS_UNCACHED | blk;
	BSDMA_CS = BSDMA_CS_ACTIVE;
	bool done = false;
	for (uint32_t i = 0; i < 4000000u; i++) {
		if (BSDMA_CS & BSDMA_CS_END) {
			done = true;
			break;
		}
	}
	BSDMA_CS = BSDMA_CS_RESET;
	BSDMA_CONBLK = 0;
	if (!done)
		return false;
	/* first and last word: did it write? */
	volatile uint32_t *d = (volatile uint32_t *)(ALIAS_UNCACHED | (dst & 0x3fffffffu));
	return d[0] == 0 && d[n / 4 - 1] == 0;
}

/* the first shown pixel of v, uncached */
static volatile uint8_t *view_base(const struct fb_view *v) {
	uint32_t bpp = v->depth / 8, pitch = v->vw * bpp;
	return (volatile uint8_t *)(ALIAS_UNCACHED | (v->base & 0x3fffffffu)) + v->yoff * pitch + v->xoff * bpp;
}

/* the panel fits into v and v is 16 or 32 bit */
static bool panel_fits(const struct fb_view *v) {
	return (v->depth == 16 || v->depth == 32) && v->w >= BS_W && v->h >= BS_H;
}

/*
 * Panel rows y0..y1-1 into the middle of v. black: also the black pixels
 * (a redraw), else they are skipped (the buffer was just cleared).
 */
static void copy_rows(const struct fb_view *v, uint32_t y0, uint32_t y1, bool black) {
	uint32_t bpp = v->depth / 8, pitch = v->vw * bpp, last_in = 0, last_out = 0;
	volatile uint8_t *base = view_base(v);
	uint32_t px = (v->w - BS_W) / 2, py = (v->h - BS_H) / 2;
	for (uint32_t y = y0; y < y1; y++) {
		const volatile uint16_t *src = (const volatile uint16_t *)(g_bs_fb + y * BS_PITCH);
		volatile uint8_t *dst = base + (py + y) * pitch + px * bpp;
		for (uint32_t x = 0; x < BS_W; x++) {
			uint32_t p = src[x];
			if (!p && !black)
				continue; /* black already */
			if (p != last_in) /* a handful of colours: convert each once per run */
				last_in = p, last_out = p ? panel_pixel(p, v->depth, v->order) : 0;
			if (bpp == 2)
				((volatile uint16_t *)dst)[x] = (uint16_t)last_out;
			else
				((volatile uint32_t *)dst)[x] = last_out;
		}
	}
}

/*
 * The panel into the ARM's new framebuffer v: its visible part black, the
 * panel in the middle (where the HVS showed it, as hvs.c centres a smaller
 * buffer too). 16 and 32 bit only; others, and a buffer smaller than the
 * panel, are only cleared or left alone.
 */
static void keep_panel(const struct fb_view *v) {
	if (v->depth != 16 && v->depth != 32)
		return;
	uint32_t bpp = v->depth / 8, pitch = v->vw * bpp;
	volatile uint8_t *base = view_base(v);
	uint32_t first = (v->base & 0x3fffffffu) + v->yoff * pitch + v->xoff * bpp;
	bool whole = v->xoff == 0 && v->w == v->vw; /* the shown rows in one piece */
	if (!whole || !dma_clear(ALIAS_UNCACHED | first, v->h * pitch)) {
		for (uint32_t y = 0; y < v->h; y++) {
			volatile uint32_t *row = (volatile uint32_t *)(base + y * pitch);
			for (uint32_t i = 0; i < v->w * bpp / 4; i++)
				row[i] = 0;
		}
	}
	if (panel_fits(v))
		copy_rows(v, 0, BS_H, false);
}

/*
 * The ARM shows its own framebuffer v now (mbox.c): stop drawing, and keep
 * the panel on the screen by copying it there. v NULL: only stop.
 */
void bs_handover(const struct fb_view *v) {
	if (!g_on) {
		/*
		 * The ARM changed its framebuffer (#43): follow it. The panel is
		 * copied again only once Redox reports steps (QUIET=1); without
		 * that, Redox's boot log may be in it already.
		 */
		if (v && g_arm && !g_done) {
			if (!panel_fits(v))
				g_arm = false;
			else {
				g_view = *v;
				if (g_redox)
					keep_panel(v);
			}
		}
		return;
	}
	g_on = false;
	g_base_pct = g_pct;
	uint32_t t0 = timer_us();
	if (v) {
		keep_panel(v);
		if (panel_fits(v)) {
			g_view = *v;
			g_arm = true;
		}
	}
	printf("bootscreen: handed over to the ARM's framebuffer %u ms after it came up; HDMI start %u us, drawing %u us, "
	       "copy %u us\n",
	       (timer_us() - g_t_up) / 1000, g_t_hdmi, g_t_draw, timer_us() - t0);
}

/*
 * One of Redox's boot steps (tag 0x000f0002, #43): pct 0..100 of Redox's
 * boot (~0: the bar stays), flags bit 0 a failed step (red), bit 1 the
 * last step (the bar full, nothing drawn after it), s the line (printable
 * ASCII, empty: no new line). 1 when drawn, 0 when there is no panel in
 * the ARM's framebuffer or the last step was before.
 */
uint32_t bs_redox_step(uint32_t pct, uint32_t flags, const char *s) {
	if (!g_arm || g_done)
		return 0;
	uint32_t t0 = timer_us();
	bool first = !g_redox; /* the whole panel then: the buffer may be new */
	g_redox = true;
	if (pct != 0xffffffffu) {
		if (pct > 100)
			pct = 100;
		uint32_t p = g_base_pct + (100 - g_base_pct) * pct / 100;
		if (p > g_pct) /* the bar never goes back */
			g_pct = p;
	}
	char line[BS_LINE_MAX + 1];
	uint32_t n = 0;
	for (; s[n] && n < BS_LINE_MAX; n++)
		line[n] = s[n] >= ' ' && s[n] <= '~' ? s[n] : ' ';
	line[n] = 0;
	if (n)
		add_line(line, 0, flags & 1);
	if (flags & 2) {
		g_pct = 100;
		g_done = true;
	}
	draw_bar(g_bs_fb);
	draw_lines(g_bs_fb);
	copy_rows(&g_view, first ? 0 : BS_BAR_Y, BS_H, true);
	g_redox_n++;
	g_redox_us += timer_us() - t0;
	if (g_done)
		printf("bootscreen: Redox's last step, %u steps, drawing %u us\n", g_redox_n, g_redox_us);
	return 1;
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

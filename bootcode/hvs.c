/*
 * redox-rpi bootcode: the HVS display list entry for the ARM's framebuffer
 * (#442 step 2). No register access here, so tests/hvs-sim.sh can check
 * the words in the resim simulator.
 *
 * One unscaled ("unity") plane, as Linux's vc4_plane.c writes it for a
 * linear RGB buffer: CTL0, POS0, POS2, POS3 (context), pointer, pointer
 * context, pitch. Field layout from Linux vc4_regs.h. Format, pixel order
 * and RGBA expansion as start.elf's plane for U-Boot's 32-bit framebuffer
 * (Pi test row 337: CTL0 4e005807, i.e. RGBA8888, order ARGB, expand
 * round; that plane was scaled for overscan, ours is not). New code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define CTL0_VALID (1u << 30)
#define CTL0_SIZE(n) ((n) << 24)
#define CTL0_ORDER(o) ((o) << 13)
#define CTL0_EXPAND_ROUND (3u << 11)
#define CTL0_UNITY (1u << 4)
#define FMT_RGB565 4u
#define FMT_RGB888 5u
#define FMT_RGBA8888 7u
/* pixel orders: for 32 bit ARGB 2, ABGR 3; for 16 and 24 bit XRGB 2, XBGR 3 */
#define ORDER_RGB 2u
#define ORDER_BGR 3u
#define POS2_ALPHA_FIXED (1u << 30)
#define CONTEXT_DUMMY 0xc0c0c0c0u /* the HVS writes these words, as in Linux */

/*
 * Write the plane for framebuffer v on a scr_w x scr_h screen to dl.
 * Returns the number of words (HVS_PLANE_WORDS), or 0 when the format has
 * no plane (8 bit, which needs a palette) or the view is empty. A buffer
 * smaller than the screen is centred; a larger one is cut at the right and
 * bottom. The virtual offset moves the window within the buffer.
 */
uint32_t hvs_plane(uint32_t *dl, const struct fb_view *v, uint32_t scr_w, uint32_t scr_h) {
	uint32_t fmt, bpp = (v->depth + 7) / 8;
	if (v->depth == 32)
		fmt = FMT_RGBA8888;
	else if (v->depth == 24)
		fmt = FMT_RGB888;
	else if (v->depth == 16)
		fmt = FMT_RGB565;
	else
		return 0;
	if (!v->w || !v->h || v->xoff >= v->vw || v->yoff >= v->vh)
		return 0;

	uint32_t w = v->w, h = v->h;
	if (w > v->vw - v->xoff)
		w = v->vw - v->xoff;
	if (h > v->vh - v->yoff)
		h = v->vh - v->yoff;
	if (w > scr_w)
		w = scr_w;
	if (h > scr_h)
		h = scr_h;
	uint32_t x = (scr_w - w) / 2, y = (scr_h - h) / 2;
	uint32_t pitch = v->vw * bpp;

	dl[0] = CTL0_VALID | CTL0_SIZE(HVS_PLANE_WORDS) | CTL0_ORDER(v->order ? ORDER_RGB : ORDER_BGR) |
		CTL0_EXPAND_ROUND | CTL0_UNITY | fmt;
	dl[1] = (0xffu << 24) | (y << 12) | x; /* fixed alpha 0xff, start y, x */
	dl[2] = POS2_ALPHA_FIXED | (h << 16) | w;
	dl[3] = CONTEXT_DUMMY;
	dl[4] = v->base + v->yoff * pitch + v->xoff * bpp;
	dl[5] = CONTEXT_DUMMY;
	dl[6] = pitch;
	return HVS_PLANE_WORDS;
}

/*
 * Test pattern for the monitor's fb command: eight vertical colour bars
 * (white, yellow, cyan, green, magenta, red, blue, black) with a one-pixel
 * white border, in the buffer's own pixel format: 32 bit 0x00BBGGRR words for order 1
 * (0x00RRGGBB for order 0), 16 bit RGB565 (BGR565 for order 0). Returns the number of
 * pixels at the bar centres of the middle line that read back wrong.
 */
static const uint32_t g_bars[8] = {0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000};

/*
 * The HVS's pixel order means opposite things for 16 and 32 bit (#82): for
 * RGB565 order XRGB (ours for order 1) has red on top, as Linux vc4 uses
 * XRGB for DRM RGB565; for RGBA8888 order ARGB (ours for order 1, as
 * start.elf's plane for U-Boot, row 337) shows the bytes R, G, B in memory,
 * a word 0x00BBGGRR, while Linux vc4 uses ABGR for XRGB8888 (0x00RRGGBB).
 * The person test on the Zero 2 W showed the bar orange in the 16-bit panel
 * and blue after the copy into the ARM's 32-bit buffer.
 */
uint32_t fb_pixel(uint32_t rgb, uint32_t depth, uint32_t order) {
	if (depth == 16 ? !order : order)
		rgb = (rgb & 0x00ff00u) | ((rgb >> 16) & 0xffu) | ((rgb & 0xffu) << 16);
	if (depth == 16)
		return ((rgb >> 8) & 0xf800u) | ((rgb >> 5) & 0x07e0u) | ((rgb >> 3) & 0x001fu);
	return rgb;
}

static uint32_t pattern_rgb(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
	if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
		return 0xffffff;
	return g_bars[x * 8 / w];
}

uint32_t fb_pattern(volatile uint8_t *fb, uint32_t w, uint32_t h, uint32_t depth, uint32_t order, uint32_t pitch) {
	for (uint32_t y = 0; y < h; y++) {
		volatile uint8_t *row = fb + y * pitch;
		for (uint32_t x = 0; x < w; x++) {
			uint32_t p = fb_pixel(pattern_rgb(x, y, w, h), depth, order);
			if (depth == 16)
				((volatile uint16_t *)row)[x] = (uint16_t)p;
			else
				((volatile uint32_t *)row)[x] = p;
		}
	}
	uint32_t bad = 0, y = h / 2;
	for (uint32_t i = 0; i < 8; i++) {
		uint32_t x = (2 * i + 1) * w / 16;
		uint32_t want = fb_pixel(pattern_rgb(x, y, w, h), depth, order);
		uint32_t got = depth == 16 ? ((volatile uint16_t *)(fb + y * pitch))[x]
					   : ((volatile uint32_t *)(fb + y * pitch))[x];
		bad += got != want;
	}
	return bad;
}

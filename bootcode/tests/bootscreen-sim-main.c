/*
 * Test driver for the boot screen (#430) in the resim VC4 simulator
 * (vc4emul), run by bootscreen-sim.sh. bootscreen.c draws into a buffer in
 * the simulator's memory; stand-ins for hdmi.c print the plane it asks
 * for. The panel is summed per band of 16 lines and compared with
 * bootscreen-model.py. (A plain word sum, not a CRC: resim gets
 * "signext rN,0", which crc32() compiles to, wrong.)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

#define PANEL 0x02000000u /* the panel in the simulator's memory */

extern volatile uint8_t *g_bs_fb;

/* sdram.c is not linked: the 1 GB split, as before sdram_init() */
uint32_t g_ram_bytes = 0x40000000u, g_arm_mem = 0x40000000u - VC_MEM;

void uart_putc(char c) {
	*(volatile uint32_t *)0x7e215040u = (uint8_t)c;
}

bool hdmi_on(uint32_t rgb) {
	(void)rgb;
	return true;
}

void hdmi_fb(const struct fb_view *v) {
	uint32_t dl[HVS_PLANE_WORDS];
	uint32_t n = hvs_plane(dl, v, 1920, 1080);
	printf("PLANE %ux%u %u bit order %u:", v->w, v->h, v->depth, v->order);
	for (uint32_t i = 0; i < n; i++)
		printf(" %08X", dl[i]);
	printf("\n");
}

/* s1 += word, s2 += s1 over each band of 16 lines, and over the whole panel */
static void sums(const char *what) {
	const volatile uint32_t *p = (const volatile uint32_t *)g_bs_fb;
	uint32_t words = BS_W / 2 * 16, a1 = 0, a2 = 0;
	printf("RESULT %s", what);
	for (uint32_t band = 0; band < BS_H / 16; band++) {
		uint32_t s1 = 0, s2 = 0;
		for (uint32_t i = 0; i < words; i++) {
			uint32_t w = p[band * words + i];
			s1 += w;
			s2 += s1;
			a1 += w;
			a2 += a1;
		}
		printf(" %08X", s2);
	}
	printf(" all %08X %08X\n", a1, a2);
}

#define ARMFB 0x02100000u /* the ARM's framebuffer in the simulator's memory */

/* what the panel pixel p must become, written out apart from bootscreen.c */
static uint32_t want_pixel(uint32_t p, uint32_t depth, uint32_t order) {
	uint32_t r = ((p >> 11) * 255 + 15) / 31, g = ((p >> 5 & 63) * 255 + 31) / 63, b = ((p & 31) * 255 + 15) / 31;
	if (!order) {
		uint32_t t = r;
		r = b, b = t;
	}
	if (depth == 16)
		return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
	return r << 16 | g << 8 | b;
}

/* hand over to a w x h framebuffer (rows of vw, shown from row yoff) */
static void keep(uint32_t w, uint32_t h, uint32_t depth, uint32_t order, uint32_t yoff) {
	uint32_t vw = w + 16, vh = h + yoff, bpp = depth / 8, pitch = vw * bpp;
	volatile uint8_t *fb = (volatile uint8_t *)ARMFB;
	for (uint32_t i = 0; i < pitch * vh / 4; i++)
		((volatile uint32_t *)fb)[i] = 0x5a5a5a5au;
	struct fb_view v = {ARMFB, w, h, vw, vh, depth, order, 0, yoff};
	bs_handover(&v);
	uint32_t bad = 0, panel = 0, pad = 0;
	bool fits = w >= BS_W && h >= BS_H;
	uint32_t x0 = (w - BS_W) / 2, y0 = (h - BS_H) / 2;
	for (uint32_t y = 0; y < vh; y++)
		for (uint32_t x = 0; x < vw; x++) {
			volatile uint8_t *q = fb + y * pitch + x * bpp;
			uint32_t got = depth == 16 ? *(volatile uint16_t *)q : *(volatile uint32_t *)q;
			uint32_t want = 0;
			if (y < yoff || x >= w) {
				want = depth == 16 ? 0x5a5au : 0x5a5a5a5au; /* not shown: untouched */
				pad += got == want;
			} else if (fits && x >= x0 && x < x0 + BS_W && y - yoff >= y0 && y - yoff < y0 + BS_H) {
				uint32_t p = ((const volatile uint16_t *)g_bs_fb)[(y - yoff - y0) * BS_W + x - x0];
				want = want_pixel(p, depth, order);
				panel += p != 0;
			}
			bad += got != want;
		}
	printf("RESULT keep %ux%u %u bit order %u: bad %u, panel pixels %u, untouched %u\n", w, h, depth, order, bad,
	       panel, pad);
}

int main(void) {
	*(volatile uint32_t *)0x7e200004u = 2u << 12; /* GPFSEL1: GPIO 14 alt 5 */
	*(volatile uint32_t *)0x7e215004u = 1;        /* AUX_ENABLES: mini UART */
	printf("bootscreen-sim: start\n");
	g_bs_fb = (volatile uint8_t *)PANEL;
	/* memory that is not black, so a pixel the drawing misses shows */
	for (uint32_t i = 0; i < BS_W * BS_H / 2; i++)
		((volatile uint32_t *)PANEL)[i] = 0x5a5a5a5au;

	/* the monitor's bs command: the steps of a release boot */
	bs_demo();
	sums("demo");

	/* after the handover nothing is drawn */
	bs_handover(0);
	bs_step(100, "drawn after the handover", 0);
	sums("handover");

	/* a line longer than the panel is cut; a failure at the start */
	bs_start("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", " 0123456789abcdefghijklmnopqrstuvwxyz", 0);
	bs_fail("no SD card", 0);
	sums("long");

	/* the title from config.txt: comments and a lookalike key skipped */
	static const char cfg[] = "# boot_title=not this\r\nboot_titles=nor this\r\n[all]\r\n"
				  "  boot_title =  Redox on Raspberry Pi  \r\narm_64bit=1\r\n";
	if (!bs_config_title(cfg, sizeof cfg - 1))
		printf("RESULT title not found\n");
	sums("title");

	/* the handover keeps the panel (I4A): into a 32-bit RGB and a 16-bit
	 * BGR framebuffer with an offset, checked pixel by pixel */
	printf("KEEP-START\n");
	keep(800, 200, 32, 1, 0);
	bs_start("again", 0, 50);
	keep(700, 180, 16, 0, 2);
	bs_start("again", 0, 50);
	keep(600, 100, 32, 1, 0); /* smaller than the panel: only cleared */

	printf("bootscreen-sim: end\n");
	return 0;
}

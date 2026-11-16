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
	bs_handover();
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

	printf("bootscreen-sim: end\n");
	return 0;
}

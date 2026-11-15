/*
 * Test driver for #442 step 2 in the resim VC4 simulator (vc4emul), run by
 * hvs-sim.sh: framebuffer requests go through mbox.c's real tag path
 * (property(), mbox_fb_test()), and a stand-in for hdmi.c's hdmi_fb()
 * prints the HVS plane that hvs.c builds. fb_pattern() runs on small
 * buffers, and their bytes are checked against a Python model of the
 * pattern, which also gives the expected CRC-32 for the Pi test.
 *
 * Output goes to resim's mini UART; printf is the bootcode's printf.c.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "mbox.c"

#define REQ 0x00800000u     /* a request buffer in the ARM's memory */
#define PATTERN 0x02000000u /* a small framebuffer for fb_pattern() */

void uart_putc(char c) {
	*(volatile uint32_t *)0x7e215040u = (uint8_t)c;
}

void udelay(uint32_t us) {
	(void)us;
}

bool usb_power_on(void) {
	return true;
}

void sd_pullups(void) {
}

uint32_t arm_khz(void) {
	return 1200000;
}

uint32_t arm_max_mhz = 1400;

uint32_t arm_set_mhz(uint32_t mhz) {
	return mhz;
}

static void dump(const char *what, uint32_t bad, const volatile uint8_t *p, uint32_t n) {
	printf("RESULT pattern %s bad %u bytes ", what, bad);
	for (uint32_t i = 0; i < n; i++)
		printf("%02X", p[i]);
	printf("\n");
}

/* hdmi.c's hdmi_fb() without the hardware: print the plane */
void hdmi_fb(const struct fb_view *v) {
	if (!v) {
		printf("PLANE released\n");
		return;
	}
	uint32_t dl[HVS_PLANE_WORDS];
	uint32_t n = hvs_plane(dl, v, 1920, 1080);
	printf("PLANE %ux%u %u bit order %u:", v->w, v->h, v->depth, v->order);
	if (!n)
		printf(" none");
	for (uint32_t i = 0; i < n; i++)
		printf(" %08X", dl[i]);
	printf("\n");
}

/* one property request with the given words (after size and code) */
static volatile uint32_t *request(const uint32_t *w, uint32_t n) {
	volatile uint32_t *b = (volatile uint32_t *)(ALIAS_UNCACHED | REQ);
	b[1] = 0;
	for (uint32_t i = 0; i < n; i++)
		b[2 + i] = w[i];
	b[2 + n] = 0;
	b[0] = 4 * (3 + n);
	property(REQ);
	return b;
}

int main(void) {
	*(volatile uint32_t *)0x7e200004u = 2u << 12; /* GPFSEL1: GPIO 14 alt 5 */
	*(volatile uint32_t *)0x7e215004u = 1;        /* AUX_ENABLES: mini UART */
	printf("hvs-sim: start\n");

	/* U-Boot first asks for the display size */
	static const uint32_t size[] = {0x00040003, 8, 0, 0, 0};
	volatile uint32_t *b = request(size, sizeof size / 4);
	printf("RESULT size %u %u\n", b[5], b[6]);

	/* U-Boot's framebuffer: 1920x1080, 32 bit, RGB */
	uint32_t base, pitch;
	bool ok = mbox_fb_test(1920, 1080, 32, 1, &base, &pitch);
	printf("RESULT fb32 %s base %08X pitch %u\n", ok ? "ok" : "BAD", base, pitch);

	/* a smaller 16-bit BGR buffer: centred */
	ok = mbox_fb_test(1024, 768, 16, 0, &base, &pitch);
	printf("RESULT fb16 %s base %08X pitch %u\n", ok ? "ok" : "BAD", base, pitch);

	/* double buffering: virtual height twice, then the second half */
	static const uint32_t virt[] = {0x00048004, 8, 8, 1024, 1536};
	request(virt, sizeof virt / 4);
	static const uint32_t off[] = {0x00048009, 8, 8, 0, 768};
	request(off, sizeof off / 4);

	/* 8 bit has no plane; then release */
	static const uint32_t d8[] = {0x00048005, 4, 4, 8};
	request(d8, sizeof d8 / 4);
	static const uint32_t rel[] = {0x00048001, 0, 0};
	request(rel, sizeof rel / 4);
	/* after the release, a size change shows nothing */
	static const uint32_t again[] = {0x00048003, 8, 8, 800, 600};
	request(again, sizeof again / 4);
	printf("RESULT after-release done\n");

	/* the pattern on small buffers, byte for byte (the CRC is not checked
	 * here: resim gets "signext rN,0", which crc32() compiles to, wrong) */
	volatile uint8_t *fb = (volatile uint8_t *)PATTERN;
	for (uint32_t i = 0; i < 256; i++)
		fb[i] = 0;
	uint32_t bad = fb_pattern(fb, 16, 4, 32, 1, 64);
	dump("16x4x32 RGB", bad, fb, 256);
	for (uint32_t i = 0; i < 256; i++)
		fb[i] = 0; /* the pad at the end of each line stays 0 */
	bad = fb_pattern(fb, 16, 4, 16, 0, 40);
	dump("16x4x16 BGR pitch 40", bad, fb, 160);

	printf("hvs-sim: end\n");
	return 0;
}

/*
 * redox-rpi bootcode: load armstub8, the kernel and the devicetree from
 * the SD card's FAT partition and start the ARM in AArch64 (#230 M2/M3).
 * Used by the monitor's boot64 command and by the release build (BOOT=1).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hw.h"

uint32_t crc32(const volatile uint8_t *p, uint32_t n) {
	uint32_t crc = 0xffffffffu;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
	}
	return ~crc;
}

/*
 * boot64 [KERNEL [DTB]]: load armstub8, the kernel and the devicetree from
 * the FAT partition, as Broadcom's start.elf does, and start the ARM in
 * AArch64. ARM addresses: stub at 0, kernel at 0x80000, devicetree at
 * 0x2000000 (bus 0xc... = ARM 0x0...). DTB "-" skips the devicetree.
 * Without DTB: by board (revision in OTP), redox-3bp.dtb or, on a Zero
 * 2 W, redox-zero2w.dtb (Broadcom's devicetree with the overlays of
 * redox-rpi's config.txt already applied, from redox-rpi/make-dtb.sh),
 * else Broadcom's bcm2710-rpi-3-b-plus.dtb or bcm2710-rpi-zero-2-w.dtb
 * as it is. When the card has an initramfs, it goes to ARM 0x2100000 and
 * /chosen linux,initrd-start/-end get its place, as start.elf does for
 * "initramfs initramfs followkernel" (direct boot without U-Boot, #31). We don't apply
 * overlays on the VPU. DTB "@N" (hex size) uses the N bytes already at
 * ARM 0x2000000, sent over serial with "l c2000000 N C", so a devicetree
 * can be tried without writing the card (the monitor passes its size as
 * dtb_in_mem). Returns only on an error; on success the VPU stays in
 * mbox_serve().
 */
#define B64_STUB 0xc0000000u
#define B64_KERNEL 0xc0080000u
#define B64_DTB 0xc2000000u
#define B64_INITRD 0xc2100000u
#define B64_ARM(bus) ((bus) - 0xc0000000u)
#define BOARD_ZERO2W 0x12u /* board type, revision bits 4..11 */
#define B64_MEM_SIZE MBOX_ARM_MEM_SIZE

/* the boot screen (#430): "loading NAME", the bar to pct */
static bool load_one(const char *name, uint32_t addr, uint32_t max, uint32_t pct) {
	bs_step(pct, "loading ", name);
	int32_t n = fat_load(name, addr, max);
	if (n < 0)
		return false;
	printf("boot64: %s %u bytes at ARM %08X, crc32 %08X\n", name, (uint32_t)n, B64_ARM(addr),
	       crc32((const volatile uint8_t *)addr, (uint32_t)n));
	return true;
}

/* n in decimal into d (room for 11 bytes); returns the end */
static char *put_dec(char *d, uint32_t n) {
	char t[10];
	uint32_t k = 0;
	do
		t[k++] = (char)('0' + n % 10);
	while (n /= 10);
	while (k)
		*d++ = t[--k];
	*d = 0;
	return d;
}

/*
 * A large initramfs takes long on the SD card (#34): the newest boot
 * screen line counts the MB and the bar moves from 65 to 74 percent, in
 * steps of 5 percent of the file; the serial console gets a line every
 * 20 percent, so a slow boot does not look hung.
 */
static uint32_t g_load_step;

static void initrd_progress(uint32_t done, uint32_t total) {
	uint32_t part = total / 20 ? total / 20 : 1;
	uint32_t s = done / part;
	if (s == g_load_step || s > 20)
		return;
	g_load_step = s;
	char txt[32], *e = put_dec(txt, done >> 20);
	const char *of = " of ";
	while (*of)
		*e++ = *of++;
	e = put_dec(e, total >> 20);
	*e++ = ' ', *e++ = 'M', *e++ = 'B', *e = 0;
	bs_update(65 + s * 9 / 20, "loading initramfs ", txt);
	if (s % 4 == 0)
		printf("boot64: initramfs %u%%, %s\n", s * 5, txt);
}

/*
 * The initramfs for a direct boot (#31): nothing to do when the card has
 * none (U-Boot boots); else load it after the devicetree and tell the
 * kernel8.img stub where it is. False on an error.
 */
static bool load_initrd(uint8_t *dtb, uint32_t dtb_len) {
	if (fat_size("initramfs") < 0)
		return true;
	bs_step(65, "loading ", "initramfs");
	uint32_t t0 = timer_us();
	g_load_step = 0;
	g_fat_progress = initrd_progress;
	int32_t n = fat_load("initramfs", B64_INITRD, B64_MEM_SIZE - B64_ARM(B64_INITRD));
	g_fat_progress = 0;
	if (n < 0) {
		bs_fail("could not load ", "initramfs");
		return false;
	}
	uint32_t ms = (timer_us() - t0) / 1000;
	printf("boot64: initramfs %u bytes at ARM %08X in %u ms (%u KB/s)\n", (uint32_t)n, B64_ARM(B64_INITRD), ms,
	       ms ? (uint32_t)n / ms : 0);
	if (!fdt_set_initrd(dtb, dtb_len, B64_ARM(B64_INITRD), B64_ARM(B64_INITRD) + (uint32_t)n)) {
		bs_fail("no initrd place in the devicetree", 0);
		return false;
	}
	return true;
}

bool boot64(const char *kernel, const char *dtb, uint32_t dtb_in_mem) {
	if (!g_sdram_up) {
		printf("err boot64: SDRAM not set up\n");
		return false;
	}
#if HDMI
	/* the boot screen's title from the card (boot_title= in config.txt);
	 * read into the devicetree's place, which is loaded later */
	int32_t cn = fat_size("config.txt");
	if (cn > 0 && cn <= 0x10000 && !dtb_in_mem && fat_load("config.txt", B64_DTB, 0x10000u) == cn &&
	    bs_config_title((const char *)B64_DTB, (uint32_t)cn))
		printf("boot64: boot screen title from config.txt\n");
#endif
	if (!load_one("armstub8-redox.bin", B64_STUB, B64_KERNEL - B64_STUB, 30) &&
	    !load_one("armstub8.bin", B64_STUB, B64_KERNEL - B64_STUB, 30)) {
		bs_fail("not found: ", "armstub8.bin");
		return false;
	}
	if (!load_one(kernel, B64_KERNEL, B64_DTB - B64_KERNEL, 45)) {
		bs_fail("not found: ", kernel);
		return false;
	}
	volatile uint32_t *stub = (volatile uint32_t *)B64_STUB;
	stub[0xfc / 4] = B64_ARM(B64_KERNEL); /* kernel_entry32 */
	stub[0xf8 / 4] = 0;                   /* dtb_ptr32 */
	if (!dtb || dtb[0] != '-' || dtb[1]) {
		int32_t n = -1;
		uint32_t mem = 0;
		if (dtb_in_mem) {
			mem = dtb_in_mem;
			n = (int32_t)mem;
			dtb = "devicetree in memory";
			printf("boot64: devicetree in memory, %u bytes, crc32 %08X\n", mem,
			       crc32((const volatile uint8_t *)B64_DTB, mem));
		} else if (!dtb) {
			uint32_t rev = board_revision();
			bool zero = ((rev >> 4) & 0xffu) == BOARD_ZERO2W;
			printf("boot64: board revision %06X%s\n", rev, zero ? ", Zero 2 W" : "");
			dtb = zero ? "redox-zero2w.dtb" : "redox-3bp.dtb";
			bs_step(60, "loading ", dtb);
			n = fat_load(dtb, B64_DTB, 0x100000u);
			if (n < 0) {
				dtb = zero ? "bcm2710-rpi-zero-2-w.dtb" : "bcm2710-rpi-3-b-plus.dtb";
				printf("boot64: no overlays applied (disable-bt, mmc): serial0 is the mini UART\n");
			}
		}
		if (n < 0) {
			bs_step(60, "loading ", dtb);
			n = fat_load(dtb, B64_DTB, 0x100000u);
		}
		if (n < 0) {
			bs_fail("not found: ", dtb);
			return false;
		}
		if (!fdt_set_memory((uint8_t *)B64_DTB, (uint32_t)n, 0, B64_MEM_SIZE))
			return false;
		stub[0xf8 / 4] = B64_ARM(B64_DTB);
		printf("boot64: %s %u bytes at ARM %08X, memory 0-%08X\n", dtb, (uint32_t)n, B64_ARM(B64_DTB), B64_MEM_SIZE);
		if (!dtb_in_mem && !load_initrd((uint8_t *)B64_DTB, (uint32_t)n))
			return false;
	}
	sd_off();
	bs_step(75, "starting the ARM", 0);
	if (arm_start(true)) {
		printf("ok boot64, the serial line is the ARM's now\n");
		mbox_serve();
	}
	bs_fail("the ARM did not start", 0);
	return false;
}


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
 * Without DTB: redox-3bp.dtb (Broadcom's devicetree with the overlays of
 * redox-rpi's config.txt already applied, from redox-rpi/make-dtb.sh),
 * else Broadcom's bcm2710-rpi-3-b-plus.dtb as it is. We don't apply
 * overlays on the VPU. DTB "@N" (hex size) uses the N bytes already at
 * ARM 0x2000000, sent over serial with "l c2000000 N C", so a devicetree
 * can be tried without writing the card (the monitor passes its size as
 * dtb_in_mem). Returns only on an error; on success the VPU stays in
 * mbox_serve().
 */
#define B64_STUB 0xc0000000u
#define B64_KERNEL 0xc0080000u
#define B64_DTB 0xc2000000u
#define B64_ARM(bus) ((bus) - 0xc0000000u)
#define B64_MEM_SIZE MBOX_ARM_MEM_SIZE

static bool load_one(const char *name, uint32_t addr, uint32_t max) {
	int32_t n = fat_load(name, addr, max);
	if (n < 0)
		return false;
	printf("boot64: %s %u bytes at ARM %08X, crc32 %08X\n", name, (uint32_t)n, B64_ARM(addr),
	       crc32((const volatile uint8_t *)addr, (uint32_t)n));
	return true;
}

bool boot64(const char *kernel, const char *dtb, uint32_t dtb_in_mem) {
	if (!g_sdram_up) {
		printf("err boot64: SDRAM not set up\n");
		return false;
	}
	if (!load_one("armstub8-redox.bin", B64_STUB, B64_KERNEL - B64_STUB) &&
	    !load_one("armstub8.bin", B64_STUB, B64_KERNEL - B64_STUB))
		return false;
	if (!load_one(kernel, B64_KERNEL, B64_DTB - B64_KERNEL))
		return false;
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
			dtb = "redox-3bp.dtb";
			n = fat_load(dtb, B64_DTB, 0x100000u);
			if (n < 0) {
				dtb = "bcm2710-rpi-3-b-plus.dtb";
				printf("boot64: no overlays applied (disable-bt, mmc): serial0 is the mini UART\n");
			}
		}
		if (n < 0)
			n = fat_load(dtb, B64_DTB, 0x100000u);
		if (n < 0)
			return false;
		if (!fdt_set_memory((uint8_t *)B64_DTB, (uint32_t)n, 0, B64_MEM_SIZE))
			return false;
		stub[0xf8 / 4] = B64_ARM(B64_DTB);
		printf("boot64: %s %u bytes at ARM %08X, memory 0-%08X\n", dtb, (uint32_t)n, B64_ARM(B64_DTB), B64_MEM_SIZE);
	}
	sd_off();
	if (arm_start(true)) {
		printf("ok boot64, the serial line is the ARM's now\n");
		mbox_serve();
	}
	return false;
}


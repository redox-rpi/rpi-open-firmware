/*
 * redox-rpi bootcode: a bootcode.bin image built into the monitor
 * (make MONITOR=1 CHAIN_IMAGE=path), which the monitor chains when nobody
 * opens it. Used for the boot selector on the test Pi's network boot card:
 * Enter on the serial line keeps the monitor, otherwise the card boots
 * as before through Broadcom's bootcode.bin. The image is copied in
 * unchanged at build time; this file only places it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.section .rodata.chain, "a"
.balign 4
.globl chain_image
.globl chain_image_end
chain_image:
	.incbin CHAIN_IMAGE_PATH
chain_image_end:

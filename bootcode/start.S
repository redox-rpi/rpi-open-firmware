/*
 * redox-rpi bootcode: entry and exception stubs.
 *
 * The Boot ROM loads bootcode.bin to 0x80000000 (L2-cached alias; before
 * SDRAM is up only the 128 KB L2 cache backs it) and jumps to 0x80000200.
 * The 0x200-byte gap and the entry address follow rpi-open-firmware's
 * firmware/start.s (GPLv2+); the rest is new.
 *
 * Memory use inside the 128 KB: code and data from 0x80000000, the
 * exception vector table at 0x8001b000 (as upstream), the stack below it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.section .text.start, "ax"

	.space 0x200

.globl _start
_start:
	version r0
	mov sp, 0x8001b000
	bl bootcode_main
1:	b 1b

/* One stub per exception number: r0 = number, r1 = stack (sr, pc pushed by the CPU). */
.text
.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
exc_stub_\n:
	mov r1, sp
	mov r0, \n
	bl exception_fatal
.endr

.section .rodata
.globl exc_stub_table
.align 2
exc_stub_table:
.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
	.word exc_stub_\n
.endr

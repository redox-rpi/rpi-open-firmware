/*
 * redox-rpi armstub8 test kernel: started by the stub like an arm64 Linux
 * Image (x0 = devicetree, MMU off), at 0x80000 as kernel8.img.
 *
 * Core 0 prints its exception level, CNTFRQ_EL0 and x0, then releases
 * cores 1-3 one after the other through the spin table (0xe0, 0xe8,
 * 0xf0), as U-Boot and Linux do. Each core prints its MPIDR and EL and
 * sets its flag; core 0 waits up to about 1 s per core. The last line is
 * "armstub8 test: 4 cores up" or names the core that did not come.
 * PL011 at 0x3F201000, already set up (115200 8N1). Position independent.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define UART	0x3F201000
#define SPIN	0xd8

	.text
	.globl	_start
_start:
	b	entry			/* arm64 Image header */
	.word	0
	.quad	0			/* text_offset */
	.quad	_end - _start		/* image_size */
	.quad	0x2			/* 4K pages, little endian */
	.quad	0, 0, 0
	.ascii	"ARM\x64"
	.word	0

entry:
	mrs	x9, mpidr_el1
	and	x9, x9, #3
	cbnz	x9, secondary

	mov	x20, x0			/* devicetree */
	ldr	x19, =UART
	adr	x0, s_banner
	bl	puts
	adr	x0, s_el
	bl	puts
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	bl	puthex
	adr	x0, s_cntfrq
	bl	puts
	mrs	x0, cntfrq_el0
	bl	puthex
	adr	x0, s_fdt
	bl	puts
	mov	x0, x20
	bl	puthex
	adr	x0, s_nl
	bl	puts

	mov	x24, #1			/* next core */
release:
	mov	x1, #SPIN
	add	x1, x1, x24, lsl #3
	adr	x2, entry
	str	x2, [x1]
	dsb	sy
	sev
	adr	x3, flags
	ldr	x25, =50000000		/* about 1 s of polling */
2:	ldr	w4, [x3, x24, lsl #2]
	cbnz	w4, 3f
	subs	x25, x25, #1
	b.ne	2b
	adr	x0, s_missing
	bl	puts
	mov	x0, x24
	bl	puthex
	adr	x0, s_nl
	bl	puts
	b	park
3:	add	x24, x24, #1
	cmp	x24, #4
	b.ne	release
	adr	x0, s_allup
	bl	puts
park:	wfe
	b	park

/* cores 1-3: core 0 is busy polling, so the UART is ours */
secondary:
	ldr	x19, =UART
	adr	x0, s_core
	bl	puts
	mrs	x0, mpidr_el1
	bl	puthex
	adr	x0, s_el2
	bl	puts
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	bl	puthex
	adr	x0, s_nl
	bl	puts
	adr	x3, flags
	mov	w4, #1
	str	w4, [x3, x9, lsl #2]
	dsb	sy
	b	park

puts:
	mov	x21, x30
	mov	x22, x0
4:	ldrb	w0, [x22], #1
	cbz	w0, 5f
	cmp	w0, #'\n'
	b.ne	6f
	mov	w0, #'\r'
	bl	putc
	mov	w0, #'\n'
6:	bl	putc
	b	4b
5:	mov	x30, x21
	ret

puthex:
	mov	x21, x30
	mov	x22, x0
	mov	w0, #'0'
	bl	putc
	mov	w0, #'x'
	bl	putc
	mov	x23, #60
7:	lsr	x0, x22, x23
	and	x0, x0, #0xF
	cmp	x0, #10
	b.lt	8f
	add	x0, x0, #('A' - 10 - '0')
8:	add	x0, x0, #'0'
	bl	putc
	subs	x23, x23, #4
	b.ge	7b
	mov	x30, x21
	ret

putc:
9:	ldr	w1, [x19, #0x18]	/* FR */
	tbnz	w1, #5, 9b		/* TXFF */
	str	w0, [x19]
	ret

s_banner:  .asciz "\n\narmstub8 test: kernel reached\n"
s_el:	   .asciz "armstub8 test: core 0 EL "
s_cntfrq:  .asciz ", CNTFRQ "
s_fdt:	   .asciz ", x0 "
s_nl:	   .asciz "\n"
s_core:	   .asciz "armstub8 test: core up, MPIDR "
s_el2:	   .asciz ", EL "
s_missing: .asciz "armstub8 test: FAIL, no answer from core "
s_allup:   .asciz "armstub8 test: 4 cores up\n"
	.balign	4
flags:	.word	0, 0, 0, 0
	.balign	8
	.ltorg
_end:

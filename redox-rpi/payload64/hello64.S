/*
 * redox-rpi 64-bit test payload (GPLv2+), for librerpi/lk-overlay's
 * linux-bootloader, which loads /boot/Image-aarch64 to 16 MiB and jumps to
 * it like an arm64 Linux kernel: x0 = FDT, MMU off.
 *
 * It prints the exception level, MIDR, MPIDR, x0, SCTLR_EL1 and CNTFRQ_EL0
 * on the PL011 UART (0x3F201000, already set up by the firmware), then
 * waits forever. Position independent; carries an arm64 Image header.
 */

	.text
	.globl	_start
_start:
	b	entry			// code0
	.word	0			// code1
	.quad	0			// text_offset
	.quad	_end - _start		// image_size
	.quad	0x2			// flags: 4K pages, little endian
	.quad	0, 0, 0			// res2-4
	.ascii	"ARM\x64"		// magic
	.word	0			// res5

entry:
	mov	x20, x0			// keep the FDT pointer
	ldr	x19, =0x3F201000	// PL011 base

	adr	x0, banner
	bl	puts

	adr	x0, s_el
	bl	puts
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	bl	puthex

	adr	x0, s_midr
	bl	puts
	mrs	x0, midr_el1
	bl	puthex

	adr	x0, s_mpidr
	bl	puts
	mrs	x0, mpidr_el1
	bl	puthex

	adr	x0, s_fdt
	bl	puts
	mov	x0, x20
	bl	puthex

	adr	x0, s_sctlr
	bl	puts
	mrs	x0, sctlr_el1
	bl	puthex

	adr	x0, s_cntfrq
	bl	puts
	mrs	x0, cntfrq_el0
	bl	puthex

	adr	x0, s_done
	bl	puts
1:	wfe
	b	1b

// x0 = NUL-terminated string
puts:
	mov	x21, x30
	mov	x22, x0
2:	ldrb	w0, [x22], #1
	cbz	w0, 3f
	cmp	w0, #'\n'
	b.ne	4f
	mov	w0, #'\r'
	bl	putc
	mov	w0, #'\n'
4:	bl	putc
	b	2b
3:	mov	x30, x21
	ret

// x0 = value, printed as 0x%016X
puthex:
	mov	x21, x30
	mov	x22, x0
	mov	w0, #'0'
	bl	putc
	mov	w0, #'x'
	bl	putc
	mov	x23, #60
5:	lsr	x0, x22, x23
	and	x0, x0, #0xF
	cmp	x0, #10
	b.lt	6f
	add	x0, x0, #('A' - 10 - '0')
6:	add	x0, x0, #'0'
	bl	putc
	subs	x23, x23, #4
	b.ge	5b
	mov	x30, x21
	ret

// w0 = byte
putc:
7:	ldr	w1, [x19, #0x18]	// FR
	tbnz	w1, #5, 7b		// TXFF
	str	w0, [x19]		// DR
	ret

banner:	.asciz	"\n\nredox-rpi: 64-bit test payload reached (loaded as Image-aarch64)\n"
s_el:	.asciz	"redox-rpi: EL     = "
s_midr:	.asciz	"\nredox-rpi: MIDR   = "
s_mpidr: .asciz	"\nredox-rpi: MPIDR  = "
s_fdt:	.asciz	"\nredox-rpi: x0 FDT = "
s_sctlr: .asciz	"\nredox-rpi: SCTLR  = "
s_cntfrq: .asciz "\nredox-rpi: CNTFRQ = "
s_done:	.asciz	"\nredox-rpi: done, waiting (wfe)\n"
	.balign	8
	.ltorg
_end:

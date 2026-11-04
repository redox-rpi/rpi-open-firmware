/*
 * redox-rpi test payload for rpi-open-firmware (GPLv2+).
 *
 * The ARM chainloader loads the file "zImage" to 0x2000000 and calls it
 * like a 32-bit Linux kernel: r0 = 0, r1 = ~0 (machine type), r2 = FDT.
 * This payload only proves that hand-off: it prints a banner, the CPU
 * mode (CPSR), MIDR, MPIDR and r2 on the PL011 UART that the firmware
 * already set up (GPIO 14/15, 115200 baud), then waits forever.
 * Linked at 0x2000000 (see Makefile).
 */

	.arch armv7-a
	.syntax unified
	.text
	.globl _start
_start:
	mov	r8, r2			@ keep the FDT pointer
	ldr	r4, =0x3F201000		@ PL011 base, ARM view

	ldr	r0, =banner
	bl	puts

	ldr	r0, =s_cpsr
	bl	puts
	mrs	r0, cpsr
	bl	puthex

	ldr	r0, =s_midr
	bl	puts
	mrc	p15, 0, r0, c0, c0, 0
	bl	puthex

	ldr	r0, =s_mpidr
	bl	puts
	mrc	p15, 0, r0, c0, c0, 5
	bl	puthex

	ldr	r0, =s_fdt
	bl	puts
	mov	r0, r8
	bl	puthex

	ldr	r0, =s_done
	bl	puts
1:	wfe
	b	1b

@ r0 = NUL-terminated string
puts:
	push	{r5, lr}
	mov	r5, r0
2:	ldrb	r0, [r5], #1
	cmp	r0, #0
	popeq	{r5, pc}
	cmp	r0, #'\n'
	bne	5f
	mov	r0, #'\r'
	bl	putc
	mov	r0, #'\n'
5:	bl	putc
	b	2b

@ r0 = value, printed as 0x%08X
puthex:
	push	{r5, r6, lr}
	mov	r5, r0
	mov	r0, #'0'
	bl	putc
	mov	r0, #'x'
	bl	putc
	mov	r6, #28
3:	lsr	r0, r5, r6
	and	r0, r0, #0xF
	cmp	r0, #10
	addlt	r0, r0, #'0'
	addge	r0, r0, #('A' - 10)
	bl	putc
	subs	r6, r6, #4
	bge	3b
	pop	{r5, r6, pc}

@ r0 = byte
putc:
4:	ldr	r1, [r4, #0x18]		@ FR
	tst	r1, #0x20		@ TXFF
	bne	4b
	str	r0, [r4]		@ DR
	bx	lr

banner:	.asciz	"\n\nredox-rpi: test payload reached (32-bit ARM, loaded as zImage)\n"
s_cpsr:	.asciz	"redox-rpi: CPSR  = "
s_midr:	.asciz	"\nredox-rpi: MIDR  = "
s_mpidr: .asciz	"\nredox-rpi: MPIDR = "
s_fdt:	.asciz	"\nredox-rpi: r2 (FDT) = "
s_done:	.asciz	"\nredox-rpi: done, waiting (wfe)\n"
	.align	2
	.ltorg

/*
 * redox-rpi armstub8: our own 64-bit ARM start stub for the Pi 3B+
 * (BCM2837B0, 4x Cortex-A53).
 *
 * The ARM cores come out of reset at address 0 in EL3, AArch64. This stub
 * sets up what only EL3 can set up, drops every core to EL2 and then:
 *   core 0     jumps to the kernel (kernel_entry32, default 0x80000) with
 *              x0 = devicetree (dtb_ptr32), x1-x3 = 0, MMU and caches off,
 *              as the arm64 Linux boot protocol wants it;
 *   cores 1-3  wait with wfe on their spin-table slot (0xe0, 0xe8, 0xf0)
 *              until the kernel writes an entry address there and sends
 *              sev, then jump to it with x0-x3 = 0.
 * U-Boot and Linux find the slots through "cpu-release-addr" in the
 * devicetree; core 0's slot (0xd8) is never used.
 *
 * Interface: the last 16 bytes are the layout that Broadcom's start.elf
 * patches in an armstub (magic 0x5afe570b at 0xf0, version at 0xf4, then
 * the devicetree and kernel addresses), as in the published
 * raspberrypi/tools armstub8.S. Our VPU loader (M2) fills the same
 * fields, so either firmware can start this stub. Core 3's slot overlaps
 * the magic: a slot that still holds the magic counts as empty.
 *
 * New code. Register meanings from the Arm Architecture Reference
 * Manual (SCR_EL3, SPSR, SCTLR_EL2) and the Cortex-A53 TRM (CPUECTLR_EL1,
 * ACTLR_EL3); the ARM local timer registers (0x40000000, 0x40000008)
 * from Broadcom's "Quad-A7 control" document for the BCM2836/7.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define LOCAL_BASE	0x40000000	/* ARM local peripherals */
#define LOCAL_CONTROL	0x00		/* timer source: 0 = 19.2 MHz crystal */
#define LOCAL_PRESCALER	0x08		/* 0x80000000 = divide by 1 */
#define OSC_FREQ	19200000

#define SCR_NS		(1 << 0)	/* lower ELs are non-secure */
#define SCR_RES1	(3 << 4)
#define SCR_SMD		(1 << 7)	/* no SMC: nothing answers it */
#define SCR_HCE		(1 << 8)	/* HVC allowed */
#define SCR_RW		(1 << 10)	/* EL2 is AArch64 */
#define SCR_VALUE	(SCR_NS | SCR_RES1 | SCR_SMD | SCR_HCE | SCR_RW)

#define SPSR_EL2H	0x3c9		/* EL2 with SP_EL2, D A I F masked */
#define SCTLR_EL2_OFF	0x30c50830	/* RES1 bits only: MMU, caches off */
#define ACTLR_A53	0x73		/* lower ELs may use CPUACTLR, CPUECTLR,
					   L2CTLR, L2ECTLR, L2ACTLR */
#define CPUECTLR_SMPEN	(1 << 6)	/* take part in cache coherency */

#define STUB_MAGIC	0x5afe570b

	.text
	.globl	_start
_start:
	mrs	x6, CurrentEL
	cmp	x6, #(3 << 2)
	b.ne	in_el2			/* started below EL3 (some emulators) */

	ldr	x0, =OSC_FREQ
	msr	cntfrq_el0, x0
	msr	cntvoff_el2, xzr

	mov	x1, #LOCAL_BASE
	str	wzr, [x1, #LOCAL_CONTROL]
	mov	w2, #0x80000000
	str	w2, [x1, #LOCAL_PRESCALER]

	mov	x0, #ACTLR_A53
	msr	actlr_el3, x0
	msr	actlr_el2, x0
	mrs	x0, S3_1_C15_C2_1	/* CPUECTLR_EL1 */
	orr	x0, x0, #CPUECTLR_SMPEN
	msr	S3_1_C15_C2_1, x0

	msr	cptr_el3, xzr		/* no FP/SIMD traps */
	mov	x0, #SCR_VALUE
	msr	scr_el3, x0
	ldr	x0, =SCTLR_EL2_OFF
	msr	sctlr_el2, x0
	mov	x0, #SPSR_EL2H
	msr	spsr_el3, x0
	adr	x0, in_el2
	msr	elr_el3, x0
	eret

in_el2:
	mrs	x6, mpidr_el1
	ands	x6, x6, #3
	b.eq	primary

	adr	x5, spin_cpu0
	add	x5, x5, x6, lsl #3
	ldr	x7, magic_slot
1:	wfe
	ldr	x4, [x5]
	cbz	x4, 1b
	cmp	x4, x7
	b.eq	1b
	mov	x0, xzr
	b	boot

primary:
	ldr	w4, kernel_entry32
	ldr	w0, dtb_ptr32
boot:
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x4

	.balign	8
magic_slot:
	.word	STUB_MAGIC, 0		/* what core 3's slot holds untouched */
	.ltorg

	.org	0xd8
	.globl	spin_cpu0
spin_cpu0:
	.quad	0
	.org	0xe0
spin_cpu1:
	.quad	0
	.org	0xe8
spin_cpu2:
	.quad	0
	.org	0xf0
spin_cpu3:				/* overlaps the next two words */
stub_magic:
	.word	STUB_MAGIC
	.org	0xf4
stub_version:
	.word	0
	.org	0xf8
	.globl	dtb_ptr32
dtb_ptr32:
	.word	0
	.org	0xfc
	.globl	kernel_entry32
kernel_entry32:
	.word	0x80000

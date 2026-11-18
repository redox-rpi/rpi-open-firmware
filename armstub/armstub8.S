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
 * PSCI (rof #40): SMC is on, and an EL3 vector table at 0x800 answers
 * the PSCI 1.0 calls Redox and Linux use to stop or restart the Pi:
 *   PSCI_VERSION   1.0
 *   PSCI_FEATURES  0 for the four calls here, NOT_SUPPORTED otherwise
 *   SYSTEM_OFF     set the halt partition (63) in PM_RSTS and reset through
 *                  the PM watchdog, as Linux does with Broadcom's firmware;
 *                  our bootcode then stays off until GPIO3 goes low
 *   SYSTEM_RESET   reset through the PM watchdog
 * Everything else answers NOT_SUPPORTED: the cores still start through
 * the spin table, not CPU_ON. The word "PSCI" at 0x100 tells our VPU
 * loader that this stub answers PSCI, so it adds a /psci node to the
 * devicetree. The stub is 0xc04 bytes and must stay inside the first
 * 4 KiB, which the devicetree's /memreserve/ keeps from the kernel.
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
#define SCR_HCE		(1 << 8)	/* HVC allowed */
#define SCR_RW		(1 << 10)	/* EL2 is AArch64; SMD (bit 7) stays 0:
					   SMC is on and lands in el3_vectors */
#define SCR_VALUE	(SCR_NS | SCR_RES1 | SCR_HCE | SCR_RW)

#define SPSR_EL2H	0x3c9		/* EL2 with SP_EL2, D A I F masked */
#define SCTLR_EL2_OFF	0x30c50830	/* RES1 bits only: MMU, caches off */
#define ACTLR_A53	0x73		/* lower ELs may use CPUACTLR, CPUECTLR,
					   L2CTLR, L2ECTLR, L2ACTLR */
#define CPUECTLR_SMPEN	(1 << 6)	/* take part in cache coherency */

#define STUB_MAGIC	0x5afe570b

/* PSCI function IDs (SMC32 convention, also used from AArch64) */
#define PSCI_VERSION	0x84000000
#define PSCI_OFF_OFF	8		/* SYSTEM_OFF = PSCI_VERSION + 8 */
#define PSCI_RESET_OFF	9		/* SYSTEM_RESET */
#define PSCI_FEAT_OFF	10		/* PSCI_FEATURES */
#define EC_SMC64	0x17		/* ESR_EL3.EC: SMC from AArch64 */

/* power management block, BCM2837 (Pi 3, 3B+, Zero 2 W) */
#define PM_BASE		0x3f100000
#define PM_RSTC		0x1c
#define PM_RSTS		0x20
#define PM_WDOG		0x24
#define PM_PASSWORD	0x5a000000
#define PM_RSTS_HALT	0x555		/* partition 63, bits spread out */
#define PM_RSTC_WRCFG	0x30
#define PM_RSTC_FULL	0x20

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
	adr	x0, el3_vectors		/* SMC from EL1/EL2: PSCI */
	msr	vbar_el3, x0
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

/* our VPU loader looks for this word: the stub answers PSCI */
	.org	0x100
psci_marker:
	.ascii	"PSCI"
	.word	0x10000			/* PSCI 1.0 */

/*
 * SMC from a lower EL. x0 = function ID, x1 = argument. The result goes
 * back in x0; x9-x11 are scratch (SMCCC: the caller keeps nothing in
 * x4-x17 across the call). MMU and caches are off in EL3, so the PM
 * registers are reached directly.
 */
psci_smc:
	mrs	x9, esr_el3
	lsr	x9, x9, #26
	cmp	x9, #EC_SMC64
	b.ne	not_supported
	ldr	w9, =PSCI_VERSION
	cmp	w0, w9
	b.eq	psci_version
	sub	w10, w0, w9
	cmp	w10, #PSCI_OFF_OFF
	b.eq	system_off
	cmp	w10, #PSCI_RESET_OFF
	b.eq	system_reset
	cmp	w10, #PSCI_FEAT_OFF
	b.eq	psci_features
not_supported:
	mov	x0, #-1			/* NOT_SUPPORTED */
	eret

psci_version:
	mov	x0, #0x10000		/* 1.0 */
	eret

psci_features:				/* x1 = the function asked about */
	sub	w10, w1, w9
	cmp	w1, w9
	b.eq	1f
	cmp	w10, #PSCI_OFF_OFF
	b.eq	1f
	cmp	w10, #PSCI_RESET_OFF
	b.eq	1f
	cmp	w10, #PSCI_FEAT_OFF
	b.ne	not_supported
1:	mov	x0, xzr
	eret

system_off:
	ldr	x9, =PM_BASE
	ldr	w10, [x9, #PM_RSTS]
	and	w10, w10, #0x00ffffff
	ldr	w11, =(PM_PASSWORD | PM_RSTS_HALT)
	orr	w10, w10, w11
	str	w10, [x9, #PM_RSTS]
	/* fall through: the reset with the halt partition set */
system_reset:
	ldr	x9, =PM_BASE
	ldr	w10, =(PM_PASSWORD | 10)	/* about 150 us */
	str	w10, [x9, #PM_WDOG]
	ldr	w10, [x9, #PM_RSTC]
	and	w10, w10, #0x00ffffff
	bic	w10, w10, #PM_RSTC_WRCFG
	ldr	w11, =(PM_PASSWORD | PM_RSTC_FULL)
	orr	w10, w10, w11
	str	w10, [x9, #PM_RSTC]
2:	wfi
	b	2b
	.ltorg

/* EL3 vectors: only "synchronous from a lower EL in AArch64" is used */
	.balign	0x800
el3_vectors:
	.org	el3_vectors + 0x200	/* current EL with SP_ELx: should not happen */
	b	.
	.org	el3_vectors + 0x400	/* lower EL, AArch64, synchronous */
	b	psci_smc

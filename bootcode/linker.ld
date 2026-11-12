/* redox-rpi bootcode: runs from the L2 cache at 0x80000000, 128 KB. */
ENTRY(_start)

MEMORY
{
	l2 (rwx) : ORIGIN = 0x80000000, LENGTH = 0x1b000
}

SECTIONS
{
	.text : {
		KEEP(*(.text.start))
		*(.text .text.*)
	} > l2

	.rodata : { *(.rodata .rodata.*) } > l2
	.data : { *(.data .data.*) } > l2

	/* position independent, copied to 0x80019000 before use */
	.tramp : {
		__start_tramp = .;
		KEEP(*(.tramp))
		__stop_tramp = .;
	} > l2
	ASSERT(__stop_tramp - __start_tramp <= 0x800, "chain trampoline over 2 KB")

	.bss (NOLOAD) : {
		. = ALIGN(4);
		__start_bss = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		__stop_bss = .;
	} > l2

	/* the stack grows down from the vector table at 0x8001b000 */
	__stack_top = 0x8001b000;
	ASSERT(__stop_bss <= 0x80018000, "bootcode leaves less than 12 KB of stack")

	/DISCARD/ : { *(.comment) *(.note*) }
}

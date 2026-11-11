/* redox-rpi bootcode: runs from the L2 cache at 0x80000000, 128 KB. */
ENTRY(_start)

MEMORY
{
	l2 (rwx) : ORIGIN = 0x80000000, LENGTH = 0x1b000
	/* monitor.c's chain trampoline runs here, above any chained image */
	tramp (rwx) : ORIGIN = 0x80019000, LENGTH = 0x800
}

SECTIONS
{
	.text : {
		KEEP(*(.text.start))
		*(.text .text.*)
	} > l2

	.rodata : { *(.rodata .rodata.*) } > l2
	.data : { *(.data .data.*) } > l2

	/* stored in the image, copied to 0x80019000 before use */
	.tramp : {
		__tramp_start = .;
		KEEP(*(.tramp))
		__tramp_end = .;
	} > tramp AT> l2
	__tramp_load = LOADADDR(.tramp);

	.bss (NOLOAD) : {
		. = ALIGN(4);
		__bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > l2

	/* the stack grows down from the vector table at 0x8001b000 */
	__stack_top = 0x8001b000;
	ASSERT(__bss_end <= 0x80018000, "bootcode leaves less than 12 KB of stack")

	/DISCARD/ : { *(.comment) *(.note*) }
}

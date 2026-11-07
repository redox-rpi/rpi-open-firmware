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

ENTRY(_head)
SECTIONS
{
	. = 0;
	.text : { KEEP(*(.text.boot)) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.data : { *(.data .data.*) }
	.bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
}

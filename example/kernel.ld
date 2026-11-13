ENTRY(_start)
PHDRS
{
  payload PT_LOAD FLAGS(7);
}
SECTIONS
{
  . = 0x900200000;
  .payload : ALIGN(0x1000) {
    *(.text .text.*)
    *(.rodata .rodata.*)
    KEEP(*(.byter_requests_start))
    KEEP(*(.byter_requests))
    KEEP(*(.byter_requests_end))
    *(.data .data.*)
    *(.got .got.*)
    *(.bss .bss.*)
    *(COMMON)
  } :payload

}

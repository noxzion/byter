/* ARM64 Image entry used by the Phone (3a) ABL through `fastboot boot`. */
	.section .text.boot, "ax"
	.global _head
	.global _start
	.global elf_offset_field
	.global elf_size_field
	.global file_size_field
_head:
	b _start
	.long 0
	.quad 0                    /* text_offset */
	.quad 0x04000000           /* image_size: reserve 64 MiB for shim and stack */
	.quad 0x000000000000000a   /* AArch64 Image flags, as used by the working A059 shim */
	.quad 0, 0, 0
	.ascii "ARM\x64"
	.long 0
	.quad 0                    /* 64: optional replacement DTB offset; zero keeps ABL's DTB */
elf_offset_field:
	.quad 0                    /* 72: patched ELF offset within this Image */
elf_size_field:
	.quad 0                    /* 80: patched ELF byte size */
file_size_field:
	.quad 0                    /* 88: patched complete Image size */

_start:
	msr daifset, #0xf
	adr x19, _head
	mov x20, x0                /* ABL DTB pointer */
	movz x1, #0x400, lsl #16   /* 64 MiB reserved image window */
	add x1, x19, x1
	sub sp, x1, #0x10000       /* top of 192 KiB stack */
	ldr x1, elf_offset_field
	add x2, x19, x1
	ldr x3, elf_size_field
	mov x0, x19
	mov x1, x20
	movz x4, #0x400, lsl #16
	ldr x5, file_size_field
	bl byter_shim_main
	cbz x0, 1f
	br x0
1:
	wfe
	b 1b

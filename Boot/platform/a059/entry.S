#include "arch/aarch64/handoff.h"
/* ARM64 Image entry used by the Phone (3a) ABL through `fastboot boot`. */
	.section .text.boot, "ax"
	.global _head
	.global _start
	.global catalog_offset_field
	.global catalog_size_field
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
catalog_offset_field:
	.quad 0                    /* 72: catalog offset within this Image */
catalog_size_field:
	.quad 0                    /* 80: catalog byte size */
file_size_field:
	.quad 0                    /* 88: patched complete Image size */

_start:
	msr daifset, #0xf
	adr x19, _head
	mov x20, x0                /* ABL DTB pointer */
	movz x1, #0x400, lsl #16   /* 64 MiB reserved image window */
	add x1, x19, x1
	sub sp, x1, #0x10000       /* top of 192 KiB stack */
	ldr x1, catalog_offset_field
	add x2, x19, x1
	ldr x3, catalog_size_field
	mov x0, x19
	mov x1, x20
	movz x4, #0x400, lsl #16
	ldr x5, file_size_field
	mrs x9, CurrentEL
	cmp x9, #0x4
	b.eq .Lcheck_el1
	cmp x9, #0x8
	b.eq .Lcheck_el2
	b .Lfailed
.Lcheck_el1:
	mrs x10, SCTLR_EL1
	b .Lcheck_sctlr
.Lcheck_el2:
	mrs x10, SCTLR_EL2
.Lcheck_sctlr:
	tst x10, #1                  /* The Android arm64 handoff requires MMU off. */
	b.ne .Lfailed
	tst x10, #4                  /* Data cache must be off and invalidated. */
	b.ne .Lfailed
	bl byter_shim_main
	cbz x0, .Lfailed
	bl byter_aarch64_handoff
.Lfailed:
	wfe
	b .Lfailed

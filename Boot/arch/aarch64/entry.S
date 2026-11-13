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
	cbz x0, 1f
	ldr x20, [x0, #0]          /* Kernel virtual entry point. */
	ldr x21, [x0, #8]          /* TTBR0 identity map. */
	ldr x22, [x0, #16]         /* TTBR1 higher-half map. */
	ldr x23, [x0, #24]         /* TCR_EL1. */
	mrs x9, CurrentEL
	cmp x9, #0x8
	b.eq .Ldrop_to_el1
	cmp x9, #0x4
	b.eq .Lenable_el1
	b .Lfailed
.Ldrop_to_el1:
	mov x9, sp
	msr SP_EL1, x9
	mrs x9, HCR_EL2
	movz x10, #0x8000, lsl #16
	orr x9, x9, x10              /* EL1 executes AArch64. */
	msr HCR_EL2, x9
	adr x9, .Lenable_el1
	msr ELR_EL2, x9
	mov x9, #0x3c5               /* EL1h, with DAIF masked. */
	msr SPSR_EL2, x9
	eret
.Lenable_el1:
	msr daifset, #0xf
	msr TTBR0_EL1, x21
	msr TTBR1_EL1, x22
	msr TCR_EL1, x23
	mov x9, #0x44ff              /* Attr0=Normal WB, Attr1=Normal NC. */
	msr MAIR_EL1, x9
	dsb sy
	tlbi vmalle1
	dsb sy
	isb
	mrs x9, SCTLR_EL1
	orr x9, x9, #1                /* MMU */
	orr x9, x9, #4                /* Data cache */
	orr x9, x9, #0x1000           /* Instruction cache */
	msr SCTLR_EL1, x9
	isb
	br x20
1:
	.Lfailed:
	wfe
	b 1b

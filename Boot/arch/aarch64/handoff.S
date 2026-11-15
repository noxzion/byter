#include "arch/aarch64/handoff.h"

    .text
    .align  4
    .global byter_aarch64_handoff
    .type byter_aarch64_handoff, %function

/* x0 points to the architecture-defined entry/page-table context. */
byter_aarch64_handoff:
    ldr x20, [x0, #BYTER_HANDOFF_ENTRY_OFFSET]
    ldr x21, [x0, #BYTER_HANDOFF_TTBR0_OFFSET]
    ldr x22, [x0, #BYTER_HANDOFF_TTBR1_OFFSET]
    ldr x23, [x0, #BYTER_HANDOFF_TCR_OFFSET]
    mrs x9, CurrentEL
    cmp x9, #0x8
    b.eq .Ldrop_to_el1
    cmp x9, #0x4
    b.eq .Lenable_el1
    ret

.Ldrop_to_el1:
    bl byter_shim_el2_drop_stage
    mov x9, sp
    msr SP_EL1, x9
    movz x9, #0x8000, lsl #16
    msr HCR_EL2, x9
    mov x9, #3
    msr CNTHCTL_EL2, x9
    msr CPTR_EL2, xzr
    msr HSTR_EL2, xzr
    adr x9, .Lenable_el1
    msr ELR_EL2, x9
    mov x9, #0x3c5
    msr SPSR_EL2, x9
    eret

.Lenable_el1:
    msr daifset, #0xf
    msr TTBR0_EL1, x21
    msr TTBR1_EL1, x22
    msr TCR_EL1, x23
    mov x9, #0x44ff
    msr MAIR_EL1, x9
    dsb sy
    tlbi vmalle1
    dsb sy
    isb
    movz x9, #0x1805
    movk x9, #0x30d0, lsl #16
    msr SCTLR_EL1, x9
    isb
    bl byter_shim_handoff_stage
    msr ELR_EL1, x20
    mov x9, #0x3c5
    msr SPSR_EL1, x9
    msr CPACR_EL1, xzr
    isb
    mov x0, xzr
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov x30, xzr
    eret

    .size byter_aarch64_handoff, . - byter_aarch64_handoff

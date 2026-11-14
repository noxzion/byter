.section .text.vectors, "ax"
.balign 2048
.global byter_vectors
byter_vectors:
.macro byter_vector handler
	b \handler
	.space 124
.endm

/* Current EL with SP0: synchronous exceptions are recoverable during the test. */
	byter_vector byter_sync_exception
.rept 3
	byter_vector byter_exception
.endr

/* Current EL with SPx: handle SVC; keep other exceptions fatal. */
	byter_vector byter_sync_exception
.rept 3
	byter_vector byter_exception
.endr

/* Lower EL AArch64 and AArch32 vectors remain fatal. */
.rept 8
	byter_vector byter_exception
.endr

.global byter_test_svc
byter_test_svc:
	svc #0
	ret

byter_exception:
	msr daifset, #0xf
1:
	wfe
	b 1b

byter_sync_exception:
	stp x0, x1, [sp, #-16]!
	mrs x0, ESR_EL1
	adrp x1, byter_exception_esr
	str x0, [x1, :lo12:byter_exception_esr]
	adrp x1, byter_exception_count
	ldr x0, [x1, :lo12:byter_exception_count]
	add x0, x0, #1
	str x0, [x1, :lo12:byter_exception_count]
	mrs x0, ELR_EL1
	msr ELR_EL1, x0
	ldp x0, x1, [sp], #16
	eret

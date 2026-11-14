.section .text.vectors, "ax"
.balign 2048
.global byter_vectors
byter_vectors:
.rept 16
	b byter_exception
	.space 124
.endr

byter_exception:
	msr daifset, #0xf
1:
	wfe
	b 1b

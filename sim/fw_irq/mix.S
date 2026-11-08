/* uint32_t mix(uint32_t n): a loop that keeps a value in every register an interrupt could clobber (t0, t1, t2-t6, a0-a7; ra by the call itself), so an interrupt handler that does
 * not restore one of them changes the result. Returns a checksum of all of them. */
	.option arch, +zicsr
	.text
	.globl mix
mix:
	li a1, 1
	li a2, 2
	li a3, 3
	li a4, 4
	li a5, 5
	li a6, 6
	li a7, 7
	li t0, 13
	li t1, 14
	li t2, 8
	li t3, 9
	li t4, 10
	li t5, 11
	li t6, 12
1:	add a1, a1, a2
	xor a2, a2, a3
	add a3, a3, a4
	xor a4, a4, a5
	add a5, a5, a6
	xor a6, a6, a7
	add a7, a7, t0
	xor t0, t0, t1
	add t1, t1, t2
	xor t2, t2, t3
	add t3, t3, t4
	xor t4, t4, t5
	add t5, t5, t6
	xor t6, t6, a1
	addi a0, a0, -1
	bnez a0, 1b
	add a0, a1, a2
	xor a0, a0, a3
	add a0, a0, a4
	xor a0, a0, a5
	add a0, a0, a6
	xor a0, a0, a7
	add a0, a0, t0
	xor a0, a0, t1
	add a0, a0, t2
	xor a0, a0, t3
	add a0, a0, t4
	xor a0, a0, t5
	add a0, a0, t6
	ret

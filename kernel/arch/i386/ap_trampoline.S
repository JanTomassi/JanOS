.code16
.section .ap_trampoline, "ax", @progbits
.balign 16
.global ap_trampoline
.global ap_trampoline_end
.global ap_trampoline_data

.global ap_trampoline_data_offset
.global ap_trampoline_size
ap_trampoline:
	cli
	xor	%ax, %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %ss

	/* Compute physical base from SIPI vector: base = CS << 4 */
	mov	%cs, %ax
	shll	$4, %eax			/* eax = runtime base */

	/* Build GDTR at runtime using the computed base. */
	leal	ap_gdtr_runtime - ap_trampoline(%eax), %ebx
	movw	$(ap_gdt_end - ap_gdt - 1), (%ebx)	/* limit */
	leal	ap_gdt - ap_trampoline(%eax), %ecx
	movl	%ecx, 2(%ebx)				/* base */
	lgdt	(%ebx)

	mov	%cr0, %eax
	or	$0x1, %eax
	mov	%eax, %cr0

	/* Far jump with 32-bit offset (66 ea ........) */
	.byte 0x66, 0xea
	.long ap_trampoline_pmode
	.word 0x08

.code32
ap_trampoline_pmode:
	mov	$0x10, %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %ss
	mov	%ax, %fs
	mov	%ax, %gs

		/* Recover runtime base from EIP: base = (here - offset_to_start) */
		call	1f
1:		popl	%eax
		subl	$(1f - ap_trampoline), %eax

		leal	ap_trampoline_data - ap_trampoline(%eax), %eax

		movl	0(%eax), %ecx		/* page directory */
		mov	%ecx, %cr3

	mov	%cr0, %ecx
	or	$0x80000000, %ecx
	mov	%ecx, %cr0

	// mov	ap_trampoline_base_ptr, %eax
	add	$ap_trampoline_data, %eax
	mov	4(%eax), %esp		/* stack pointer */

	mov	8(%eax), %eax		/* entry */
	call	*%eax

	cli
1:	hlt
	jmp	1b

.align 8
ap_gdt:
	.quad 0x0000000000000000
	.quad 0x00cf9a000000ffff	/* code */
	.quad 0x00cf92000000ffff	/* data */
ap_gdt_end:

.align 4
ap_gdtr_runtime:
	.word 0		/* limit, patched at runtime */
	.long 0		/* base, patched at runtime */

.align 4
ap_trampoline_data:
	.long 0		/* page directory */
	.long 0		/* stack */
	.long 0		/* c entry */

ap_trampoline_end:

/* Keep helpful symbols for C side */
ap_trampoline_size:
	.long ap_trampoline_end - ap_trampoline

ap_trampoline_data_offset:
	.long ap_trampoline_data - ap_trampoline

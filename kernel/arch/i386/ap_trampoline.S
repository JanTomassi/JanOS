.code16
.section .text
.global ap_trampoline
.global ap_trampoline_end
.global ap_trampoline_data

/* NOTE: keep TRAMPOLINE_BASE in sync with SMP_TRAMPOLINE_BASE */
.set TRAMPOLINE_BASE, 0x7000

ap_trampoline:
	cli
	xor	%ax, %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %ss

	lgdt	ap_gdtr_phys

	mov	%cr0, %eax
	or	$0x1, %eax
	mov	%eax, %cr0

	ljmp	$0x08, $ap_trampoline_pmode

.code32
ap_trampoline_pmode:
	mov	$0x10, %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %ss
	mov	%ax, %fs
	mov	%ax, %gs

	movl	ap_trampoline_data_phys, %eax
	movl	0(%eax), %ecx		/* page directory */
	mov	%ecx, %cr3

	mov	%cr0, %ecx
	or	$0x80000000, %ecx
	mov	%ecx, %cr0

	movl	ap_trampoline_data_phys, %eax
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
ap_gdtr_phys:
	.word ap_gdt_end - ap_gdt - 1
	.long TRAMPOLINE_BASE + (ap_gdt - ap_trampoline)

.align 4
ap_trampoline_data_phys:
	.long TRAMPOLINE_BASE + (ap_trampoline_data - ap_trampoline)

.align 4
ap_trampoline_data:
	.long 0		/* page directory */
	.long 0		/* stack */
	.long 0		/* c entry */

ap_trampoline_end:

/* Keep helpful symbols for C side */
.global ap_trampoline_size
ap_trampoline_size:
	.long ap_trampoline_end - ap_trampoline

.global ap_trampoline_data_offset
ap_trampoline_data_offset:
	.long ap_trampoline_data - ap_trampoline

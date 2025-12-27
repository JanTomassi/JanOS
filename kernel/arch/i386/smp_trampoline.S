[BITS 16]

global smp_trampoline_start
global smp_trampoline_end
global smp_trampoline_size

smp_trampoline_start:
	cli
	xor	ax, ax
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, 0x7000

	lgdt	[gdt_descriptor]

	mov	eax, cr0
	or	eax, 0x1		; protected mode
	mov	cr0, eax
	jmp	0x08:protected_entry

[BITS 32]
protected_entry:
	mov	ax, 0x10
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	fs, ax
	mov	gs, ax

	mov	eax, [tramp_data_cr3]
	mov	cr3, eax

	mov	eax, cr0
	or	eax, 0x80000000		; enable paging
	mov	cr0, eax

	mov	esp, [tramp_data_stack]
	mov	eax, [tramp_data_entry]
	jmp	eax

gdt_start:
	dq	0
	; code
	dw	0xFFFF, 0
	db	0, 0x9A, 0xCF, 0
	; data
	dw	0xFFFF, 0
	db	0, 0x92, 0xCF, 0
gdt_end:

gdt_descriptor:
	dw	gdt_end - gdt_start - 1
	dd	gdt_start

tramp_data_stack:	dd 0
tramp_data_entry:	dd 0
tramp_data_cr3:	dd 0

smp_trampoline_end:

smp_trampoline_size equ smp_trampoline_end - smp_trampoline_start

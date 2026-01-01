[BITS 16]

section .text
global ap_trampoline_start
global ap_trampoline_end
global ap_trampoline_cr3
global ap_trampoline_entry
global ap_trampoline_stack
global ap_trampoline_idt_ptr
global ap_trampoline_stack_buf_top

ap_trampoline_start:
	cli

	mov	ax, cs
	mov	ds, ax
	mov	es, ax

	; Determine the linear base of the trampoline using the current CS and IP
	call	.get_base
.get_base:
	pop	bx
	sub	bx, .get_base - ap_trampoline_start
	movzx	ebx, bx			; EBX will hold the linear address of ap_trampoline_start
	mov	ax, ds
	shl	eax, 4
	add	ebx, eax

	; Patch the GDT descriptor base field to point at our GDT copy
	mov	eax, ebx
	add	eax, ap_trampoline_gdt - ap_trampoline_start
	mov	[bx + ap_trampoline_gdtr_base - ap_trampoline_start], eax
	lgdt	[bx + ap_trampoline_gdtr - ap_trampoline_start]

	; Prepare the protected-mode far jump target using the runtime base
	mov	eax, ebx
	add	eax, ap_trampoline_protected - ap_trampoline_start
	mov	[bx + ap_trampoline_pm_ptr - ap_trampoline_start], eax

	mov	eax, cr0
	or	eax, 1
	mov	cr0, eax

	jmp	far [bx + ap_trampoline_pm_ptr - ap_trampoline_start]

[BITS 32]
ap_trampoline_protected:
	mov	ax, 0x10
	mov	ds, ax
	mov	es, ax
	mov	fs, ax
	mov	gs, ax
	mov	ss, ax

	lea	esi, [ebx + ap_trampoline_stack - ap_trampoline_start]
	mov	esp, [esi]

	mov	eax, [ebx + ap_trampoline_cr3 - ap_trampoline_start]
	mov	cr3, eax

	mov	eax, cr0
	or	eax, 0x80000000
	mov	cr0, eax

	lidt	[ebx + ap_trampoline_idt_ptr - ap_trampoline_start]

	call	dword [ebx + ap_trampoline_entry - ap_trampoline_start]

.halt:
	hlt
	jmp	.halt

align 8
ap_trampoline_gdt:
	dq 0
	dq 0x00cf9a000000ffff	; code segment
	dq 0x00cf92000000ffff	; data segment
ap_trampoline_gdt_end:

ap_trampoline_gdtr:
	dw ap_trampoline_gdt_end - ap_trampoline_gdt - 1
ap_trampoline_gdtr_base:
	dd 0

ap_trampoline_pm_ptr:
	dd 0
	dw 0x08

align 4
ap_trampoline_cr3:	dd 0
ap_trampoline_entry:	dd 0
ap_trampoline_stack:	dd 0
ap_trampoline_idt_ptr:
	dw 0
	dd 0

align 16
ap_trampoline_stack_buf:
	resb 4096
ap_trampoline_stack_buf_top:

ap_trampoline_end:

; ---------------------------------------------------------------------------
; AP high-level entry (switch to per-CPU stack then call ap_main)
; ---------------------------------------------------------------------------
global ap_start:function
extern lapic_get_id
extern smp_get_stack_top
extern ap_main

ap_start:
	call    lapic_get_id
	mov     ebx, eax            ; preserve APIC ID
	push    eax
	call    smp_get_stack_top
	add     esp, 4
	test    eax, eax
	jz      .ap_halt

	mov     esp, eax
	mov     ebp, eax

	call    ap_main

.ap_halt:
	hlt
	jmp     .ap_halt

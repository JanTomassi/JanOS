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
	lgdt	[ap_trampoline_gdtr]

	mov	eax, cr0
	or	eax, 1
	mov	cr0, eax

	jmp	0x08:ap_trampoline_protected

[BITS 32]
ap_trampoline_protected:
	mov	ax, 0x10
	mov	ds, ax
	mov	es, ax
	mov	fs, ax
	mov	gs, ax
	mov	ss, ax

	mov	esp, [ap_trampoline_stack]

	mov	eax, [ap_trampoline_cr3]
	mov	cr3, eax

	mov	eax, cr0
	or	eax, 0x80000000
	mov	cr0, eax

	lidt	[ap_trampoline_idt_ptr]

	call	dword [ap_trampoline_entry]

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
	dd ap_trampoline_gdt

section .data
align 4
ap_trampoline_cr3:	dd 0
ap_trampoline_entry:	dd 0
ap_trampoline_stack:	dd 0
ap_trampoline_idt_ptr:
	dw 0
	dd 0

section .bss
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

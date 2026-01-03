; Trampoline blob is intended to execute from linear/physical address 0x1000.
; Keep ALL trampoline code+data+bss in one section so absolute pointers (GDTR base, etc.)
; assemble correctly for 0x1000.

section .text align=16

global ap_trampoline_start
global ap_trampoline_end
global ap_trampoline_cr3
global ap_trampoline_entry
global ap_trampoline_stack
global ap_trampoline_idt_ptr
global ap_trampoline_stack_buf_top
global ap_trampoline_gdt
global ap_trampoline_gdtr

[BITS 16]
ap_trampoline_start:
	cli
	cld

	mov	ax, cs
	mov	ds, ax
	mov	ss, ax
	mov	es, ax
	mov	sp, (ap_trampoline_stack_buf_top - ap_trampoline_start)

	; Determine the linear base of the trampoline using the current CS and IP
	call	.get_base
.get_base:
	pop	bx
	sub	bx, .get_base - ap_trampoline_start
	movzx	ebx, bx	; EBX will hold the linear address of ap_trampoline_start
	mov	ax, cs
	shl	ax, 4
	add	bx, ax

	; Prepare the protected-mode far jump target using the runtime base
	mov     ax, (ap_trampoline_protected - ap_trampoline_start)
	add     ax, bx
	mov	[ap_trampoline_pm_ptr - ap_trampoline_start], ax

	; Patch the GDT descriptor base field to point at our GDT copy
	mov     ax, (ap_trampoline_gdtr - ap_trampoline_start) ; offset from start to gdtr
	lgdt    [eax]

	; enter protected mode
	; (32-bit operands in real mode are fine on 386+)
	mov     eax, cr0
	or      eax, 1
	mov     cr0, eax

	jmp	far [ap_trampoline_pm_ptr - ap_trampoline_start]

ap_trampoline_pm_ptr:
	dw 0x4242
	dw 0x08

[BITS 32]

align 64, db 0x90		; fill with no-ops
ap_trampoline_protected:
	mov     ax, 0x10
	mov     ds, ax
	mov     es, ax
	mov     fs, ax
	mov     gs, ax
	mov     ss, ax

	lea     eax, [ebx + ap_trampoline_stack_buf_top - ap_trampoline_start]
	mov     esp, eax

	mov     eax, [ebx + (ap_trampoline_cr3 - ap_trampoline_start)]
	mov     cr3, eax

	mov     eax, cr0
	or      eax, 0x80000000 ; enable paging
	mov     cr0, eax

	lidt    [ebx + ap_trampoline_idt_ptr - ap_trampoline_start]

	mov     eax, [ebx + ap_trampoline_entry - ap_trampoline_start]
	jmp near eax

.halt:
	hlt
	jmp     .halt

align 8, db 0
ap_trampoline_gdt:
	dq 0
	dq 58434644969848831
 	dq 58425848876826623
	;; dq 0x00CF9A000000FFFF            ; code segment (0x08)
	;; dq 0x008F92000000FFFF            ; data segment (0x10)
ap_trampoline_gdt_end:

ap_trampoline_gdtr:
	dw ap_trampoline_gdt_end - ap_trampoline_gdt
	dd ap_trampoline_gdt              ; absolute linear base (now correctly 0x1000 + offset)

align 4, db 0
ap_trampoline_cr3:    dd 0
ap_trampoline_entry:  dd 0
ap_trampoline_stack:  dd 0

ap_trampoline_idt_ptr:
    dw 0
    dd 0

align 16, db 0
ap_trampoline_stack_buf:
	resb 32
ap_trampoline_stack_buf_top:

ap_trampoline_end:


; ---------------------------------------------------------------------------
; AP high-level entry (normal kernel text, not part of the 0x1000 blob)
; ---------------------------------------------------------------------------
section .text
global ap_start:function
extern lapic_get_id
extern smp_get_stack_top
extern ap_main

[BITS 32]
ap_start:
	call    lapic_get_id
	mov     ebx, eax
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

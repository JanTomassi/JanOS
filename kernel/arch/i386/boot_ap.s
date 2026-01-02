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
	jmp short .skip_data
ap_trampoline_base:	dd 0
ap_trampoline_start.skip_data:
	cli
	cld

	xor     ax, ax
	mov     ds, ax
	mov     es, ax
	mov     ss, ax

	mov     bx, (0x1000 + ap_trampoline_gdtr - ap_trampoline_start)
	lgdt    [bx]

	; enter protected mode
	; (32-bit operands in real mode are fine on 386+)
	mov     eax, cr0
	or      eax, 1
	mov     cr0, eax

	jmp     0x08:(0x1000 + ap_trampoline_protected - ap_trampoline_start)

[BITS 32]
align 64, db 0
ap_trampoline_protected:
	mov     ax, 0x10
	mov     ds, ax
	mov     es, ax
	mov     fs, ax
	mov     gs, ax
	mov     ss, ax

	mov     esp, [(0x1000 + ap_trampoline_stack - ap_trampoline_start)]

	mov     eax, [(0x1000 + ap_trampoline_cr3 - ap_trampoline_start)]
	mov     cr3, eax

	mov     eax, cr0
	or      eax, 0x80000000          ; enable paging
	mov     cr0, eax
	add 	eax, eax
	add 	eax, eax

	lidt    [(0x1000 + ap_trampoline_idt_ptr - ap_trampoline_start)]

	call    dword [(0x1000 + ap_trampoline_entry - ap_trampoline_start)]

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
	resb 4096
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

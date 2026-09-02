section .text

global i386_context_enter_asm:function
i386_context_enter_asm:
	; Keep interrupts disabled while changing address space and building iret's frame.
	cli
	mov	eax, [esp + 4]

	; A zero CR3 is never accepted by the C constructor, but avoid reloading it
	; for callers that only use this primitive with an already active address space.
	mov	edx, [eax + 52]
	test	edx, edx
	jz	.no_cr3
	mov	cr3, edx
.no_cr3:

	; DS/ES/FS/GS may use the ring-3 data descriptor while still in ring 0.
	mov	dx, [eax + 48]
	mov	ds, dx
	mov	es, dx
	mov	fs, dx
	mov	gs, dx

	; iret pops EIP, CS, EFLAGS, ESP, and SS in that order.
	push	dword [eax + 48]
	push	dword [eax + 44]
	push	dword [eax + 40]
	push	dword [eax + 36]
	push	dword [eax + 32]

	; Restore the general registers without touching the iret frame.
	mov	edi, [eax + 0]
	mov	esi, [eax + 4]
	mov	ebp, [eax + 8]
	mov	ebx, [eax + 16]
	mov	edx, [eax + 20]
	mov	ecx, [eax + 24]
	mov	eax, [eax + 28]
	iret

global i386_reaper_enter:function
i386_reaper_enter:
	; Preserve the arguments before abandoning the exiting process's stack.
	mov	eax, [esp + 4]
	mov	edx, [esp + 8]
	mov	ecx, [esp + 12]
	cli
	and	eax, 0xfffffff0
	mov	esp, eax
	push	ecx
	call	edx
.reaper_halt:
	hlt
	jmp	.reaper_halt

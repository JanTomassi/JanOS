; NASM-compatible trampoline used to bring up APs.
; This file is copied by the BSP to physical 0x8000 (code) and
; the data structure is filled separately at 0x7000.

%define TRAMPOLINE_CODE_BASE 0x8000
%define TRAMPOLINE_DATA_BASE 0x7000

%define TRAMP_DATA_STACK (TRAMPOLINE_DATA_BASE + 0)
%define TRAMP_DATA_ENTRY (TRAMPOLINE_DATA_BASE + 4)
%define TRAMP_DATA_CR3   (TRAMPOLINE_DATA_BASE + 8)

section .text
bits 16

global smp_trampoline_start
global smp_trampoline_end

smp_trampoline_start:
	cli
	xor	ax, ax
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, TRAMPOLINE_DATA_BASE

	lgdt	[gdt_descriptor]

	mov	eax, cr0
	or	eax, 0x1		; enable protected mode
	mov	cr0, eax
	jmp	0x08:protected_entry

bits 32
protected_entry:
	mov	ax, 0x10
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	fs, ax
	mov	gs, ax

	mov	eax, [TRAMP_DATA_CR3]
	mov	cr3, eax

	mov	eax, cr0
	or	eax, 0x80000000		; enable paging
	mov	cr0, eax

	mov	esp, [TRAMP_DATA_STACK]
	mov	eax, [TRAMP_DATA_ENTRY]
	jmp	eax

align 8
gdt_start:
	dq	0
	; code descriptor
	dw	0xFFFF, 0
	db	0, 0x9A, 0xCF, 0
	; data descriptor
	dw	0xFFFF, 0
	db	0, 0x92, 0xCF, 0
gdt_end:

gdt_descriptor:
	dw	gdt_end - gdt_start - 1
	dd	gdt_start

smp_trampoline_end:

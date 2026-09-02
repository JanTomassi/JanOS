bits 32

global _start
extern main
extern _Exit

section .text
_start:
	mov	eax, [esp]
	lea	edx, [esp + 4]
	and	esp, 0xfffffff0
	push	edx
	push	eax
	call main
	push	eax
	call _Exit

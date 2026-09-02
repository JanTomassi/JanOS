bits 32

global _start
extern calc_repl
extern user_exit

section .text
_start:
	and	esp, 0xfffffff0
	call calc_repl
	sub	esp, 12
	push	dword 0
	call user_exit

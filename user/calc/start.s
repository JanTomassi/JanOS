bits 32

global _start
extern calc_repl

section .text
_start:
	call calc_repl
	hlt

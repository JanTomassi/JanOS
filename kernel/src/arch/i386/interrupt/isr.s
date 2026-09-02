extern _GLOBAL_OFFSET_TABLE_
extern exception_dispatch
extern syscall_dispatch

section .data
global isr_stub_table
isr_stub_table:
	%assign i 0
	%rep 256
		dd isr_stub_%+i
	%assign i i+1
	%endrep

section .text
%macro get_GOT 0
	call %%getgot
%%getgot:
	pop ebx
	add ebx,_GLOBAL_OFFSET_TABLE_+$$-%%getgot wrt ..gotpc
%endmacro

; Entry layout after this macro is struct i386_trap_frame.  The CPU error
; code is normalized to zero for vectors that do not push one themselves.
%macro enter_frame 2
	%if %1 = 0
		push dword 0
	%endif
	 pusha
	xor eax, eax
	mov ax, ds
	push eax
	mov ax, es
	push eax
	mov ax, fs
	push eax
	mov ax, gs
	push eax
	cld
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	push dword %2
%endmacro

%macro leave_frame 1
	add esp, 4
	pop eax
	mov gs, ax
	pop eax
	mov fs, ax
	pop eax
	mov es, ax
	pop eax
	mov ds, ax
	popa
	%if %1 = 0
		add esp, 4
	%endif
	iret
%endmacro

%macro exception_noerr_stub 1
global isr_stub_%+%1:function
isr_stub_%+%1:
	enter_frame 0, %1
	get_GOT
	push esp
	call [ebx + exception_dispatch wrt ..got]
	add esp, 4
	leave_frame 0
%endmacro

%macro exception_err_stub 1
global isr_stub_%+%1:function
isr_stub_%+%1:
	enter_frame 1, %1
	get_GOT
	push esp
	call [ebx + exception_dispatch wrt ..got]
	add esp, 4
	leave_frame 1
%endmacro

%macro irq_stub 1
extern isr_%+%1_handler:function
extern irq_prepare:function
extern irq_ack:function
global isr_stub_%+%1:function
isr_stub_%+%1:
	enter_frame 0, %1
	get_GOT
	push dword %1
	call [ebx + irq_prepare wrt ..got]
	add esp, 4
	push esp
	push dword %1
	call [ebx + isr_%+%1_handler wrt ..got]
	add esp, 8
	push dword %1
	call [ebx + irq_ack wrt ..got]
	add esp, 4
	leave_frame 0
%endmacro

global syscall_entry:function
syscall_entry:
	enter_frame 0, 0x80
	get_GOT
	push esp
	call [ebx + syscall_dispatch wrt ..got]
	add esp, 4
	; syscall_dispatch stores the return value in the complete frame's EAX.
	leave_frame 0

%assign i 0
%rep 32
	%if i = 8 || (i >= 10 && i <= 14) || i = 17 || i = 21 || i = 29 || i = 30
		exception_err_stub i
	%else
		exception_noerr_stub i
	%endif
	%assign i i+1
%endrep

%assign i 32
%rep 16
	irq_stub i
	%assign i i+1
%endrep

%assign i 48
%rep 216
	exception_noerr_stub i
	%assign i i+1
%endrep

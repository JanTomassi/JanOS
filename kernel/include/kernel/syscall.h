#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <janos/syscall.h>
#include <kernel/interrupt.h>

/* i386 Linux-style register ABI: eax is the call number, ebx..ebp args. */
enum syscall_error {
	SYSCALL_EBADF = 9,
	SYSCALL_EFAULT = 14,
	SYSCALL_ESRCH = 3,
	SYSCALL_ENOSYS = 38,
};

#define SYSCALL_VECTOR 0x80

typedef struct i386_trap_frame syscall_frame;

typedef int32_t (*syscall_handler_t)(syscall_frame *frame, void *context);

void syscall_init(void);
void syscall_entry_init(void);
bool syscall_register(uint32_t number, syscall_handler_t handler, void *context);
/* Register stdin/stdout/stderr console handlers for the built-in MVP process. */
bool syscall_register_console_handlers(void);
int32_t syscall_dispatch(syscall_frame *frame);
bool copy_from_user(void *destination, const void *source, size_t length);
bool copy_to_user(void *destination, const void *source, size_t length);
bool user_buffer(uintptr_t address, size_t length, uint16_t flags);

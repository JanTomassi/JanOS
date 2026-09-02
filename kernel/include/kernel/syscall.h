#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* i386 Linux-style register ABI: eax is the call number, ebx..ebp args. */
enum syscall_number {
	SYSCALL_EXIT = 1,
	SYSCALL_WRITE = 4,
	SYSCALL_READ = 3,
	SYSCALL_MAX = 32,
};

enum syscall_error {
	SYSCALL_EBADF = 9,
	SYSCALL_EFAULT = 14,
	SYSCALL_ESRCH = 3,
	SYSCALL_ENOSYS = 38,
};

#define SYSCALL_VECTOR 0x80

struct syscall_frame {
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
};

typedef int32_t (*syscall_handler_t)(struct syscall_frame *frame, void *context);

void syscall_init(void);
void syscall_entry_init(void);
bool syscall_register(uint32_t number, syscall_handler_t handler, void *context);
/* Register stdin/stdout/stderr console handlers for the built-in MVP process. */
bool syscall_register_console_handlers(void);
int32_t syscall_dispatch(struct syscall_frame *frame);

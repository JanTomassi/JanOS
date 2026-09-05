#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <janos/syscall.h>
#include <kernel/interrupt.h>

/*
 * Kernel-side syscall dispatcher and user-memory helpers.
 *
 * The user ABI is i386 Linux-style: eax contains the call number, ebx, ecx,
 * edx, and esi contain arguments, and eax receives the result.  A handler must
 * return a negative syscall error or a non-negative result.  IPC handlers use
 * SYSCALL_EIPC_BLOCKED internally to indicate that the saved user context will
 * be completed later by the scheduler.
 */
enum syscall_error {
	/* The user supplied an invalid descriptor, endpoint, or capability. */
	SYSCALL_EBADF = 9,
	/* A user pointer failed address-space validation or copying. */
	SYSCALL_EFAULT = 14,
	/* The required process or kernel object no longer exists. */
	SYSCALL_ESRCH = 3,
	/* No handler is registered for the requested syscall number. */
	SYSCALL_ENOSYS = 38,
	/* Internal result: the syscall has saved a continuation and blocked. */
	SYSCALL_EIPC_BLOCKED = 0x7fff,
};

/* IDT vector exposed to ring-3 code for syscall entry. */
#define SYSCALL_VECTOR 0x80

typedef struct i386_trap_frame syscall_frame;

/* A registered handler receives the saved register frame and private context. */
typedef int32_t (*syscall_handler_t)(syscall_frame *frame, void *context);

/* Clear all syscall slots; call before registering handlers. */
void syscall_init(void);

/* Install the user-callable interrupt-gate entry for SYSCALL_VECTOR. */
void syscall_entry_init(void);

/*
 * Register one handler.  Returns false for an out-of-range number, a null
 * handler, or a slot that has already been registered.
 */
bool syscall_register(uint32_t number, syscall_handler_t handler, void *context);

/* Register the built-in console and utility handlers for the MVP process. */
bool syscall_register_console_handlers(void);

/*
 * Dispatch a saved frame and place a completed result in frame->eax.  A
 * blocked IPC call is the exception: its saved continuation is completed later.
 */
int32_t syscall_dispatch(syscall_frame *frame);

/* Copy bytes from a validated user address into kernel-owned storage. */
bool copy_from_user(void *destination, const void *source, size_t length);

/* Copy bytes from kernel-owned storage into a validated user address. */
bool copy_to_user(void *destination, const void *source, size_t length);

/* Validate a user virtual range against the requested page permission flags. */
bool user_buffer(uintptr_t address, size_t length, uint16_t flags);

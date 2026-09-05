#pragma once

#include <stddef.h>
#include <stdint.h>
#include <janos/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef __PTRDIFF_TYPE__ ssize_t;

/**
 * Read input from JanOS standard input.
 *
 * The current implementation accepts file descriptor 0 and waits until a
 * complete input line is available, then returns up to `length` bytes.  It
 * returns the number of bytes copied, or a negative -JANOS_* value; it does
 * not set errno.
 */
ssize_t read(int fd, void *buffer, size_t length);

/**
 * Write bytes to JanOS standard output or standard error.
 *
 * The current implementation accepts file descriptors 1 and 2 and returns
 * the number of bytes written, or a negative -JANOS_* value.
 */
ssize_t write(int fd, const void *buffer, size_t length);

/** Give up the processor so another runnable process can execute. */
int sched_yield(void);

/**
 * Drain available kernel boot/display output for the framebuffer server.
 *
 * The operation is nonblocking: zero means that no output is currently
 * buffered.  `length` may not exceed JANOS_FRAMEBUFFER_OUTPUT_CHUNK.
 */
ssize_t janos_framebuffer_read(void *buffer, size_t length);

/** Return the logical CPU index on which the calling process is running. */
int32_t janos_cpu_get(void);

/** Terminate the calling process with `status`; this function never returns. */
_Noreturn void _Exit(int status);

/**
 * Poll and, when it has exited, reap a direct child process.
 *
 * On success the child's exit status is written to `status` when non-null and
 * the child PID is returned.  JANOS_PROCESS_WAIT_NOHANG returns zero while
 * the child is still running; without it the current kernel reports
 * -JANOS_EAGAIN while the child has not exited.
 */
int32_t janos_process_wait(uint32_t pid, int32_t *status, uint32_t options);

/* IPC wrappers are documented in detail by <janos/ipc.h>. */
int32_t janos_ipc_endpoint_create(uint32_t flags);
int32_t janos_ipc_send(uint32_t endpoint, const struct janos_ipc_message *message,
                       uint32_t timeout);
int32_t janos_ipc_call(uint32_t endpoint, const struct janos_ipc_message *message,
                       struct janos_ipc_message *reply, uint32_t timeout);
int32_t janos_ipc_receive(uint32_t endpoint, struct janos_ipc_message *message,
                          uint32_t timeout);
int32_t janos_ipc_reply(uint32_t endpoint, uint32_t request_id,
                        const struct janos_ipc_message *message);
int32_t janos_ipc_notify(uint32_t endpoint, uint32_t type, uint32_t value);
int32_t janos_ipc_grant(uint32_t endpoint, uint32_t pid, uint32_t rights);
int32_t janos_ipc_cancel(uint32_t endpoint, uint32_t request_id);
int32_t janos_ipc_close(uint32_t endpoint);

#ifdef __cplusplus
}
#endif

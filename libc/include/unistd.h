#pragma once

#include <stddef.h>
#include <stdint.h>
#include <janos/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef __PTRDIFF_TYPE__ ssize_t;

ssize_t read(int fd, void *buffer, size_t length);
ssize_t write(int fd, const void *buffer, size_t length);
int sched_yield(void);
ssize_t janos_framebuffer_read(void *buffer, size_t length);
int32_t janos_cpu_get(void);
_Noreturn void _Exit(int status);

int32_t janos_process_wait(uint32_t pid, int32_t *status, uint32_t options);

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

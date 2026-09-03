#pragma once

#include <stdint.h>
#include <janos/syscall.h>

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

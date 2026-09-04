#pragma once

#include <stdbool.h>
#include <stdint.h>

struct process;
struct janos_ipc_message;

void ipc_init(void);
bool ipc_register_syscalls(void);
void ipc_process_cleanup(struct process *process);
void ipc_tick(void);
int32_t ipc_endpoint_create_for(struct process *owner);
bool ipc_grant_process(uint32_t endpoint, uint32_t process, uint32_t rights);
bool ipc_kernel_send(uint32_t endpoint, const struct janos_ipc_message *message);
bool ipc_kernel_notify(uint32_t endpoint, uint32_t type, uint32_t value);
void ipc_wake_receiver(uint32_t endpoint);

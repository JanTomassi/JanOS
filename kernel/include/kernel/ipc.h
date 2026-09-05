#pragma once

#include <stdbool.h>
#include <stdint.h>

struct process;
struct janos_ipc_message;

/* Initialize endpoint storage and its wait queues; safe to call repeatedly. */
void ipc_init(void);

/* Register all user-facing IPC syscall handlers after syscall_init(). */
bool ipc_register_syscalls(void);

/* Remove endpoint ownership, capabilities, and pending waits for an exiting process. */
void ipc_process_cleanup(struct process *process);

/* Expire timed receive/send/call waits and wake the affected processes. */
void ipc_tick(void);

/* Create an endpoint on behalf of a trusted kernel service for `owner`. */
int32_t ipc_endpoint_create_for(struct process *owner);

/* Grant or replace a capability for an existing process from trusted kernel code. */
bool ipc_grant_process(uint32_t endpoint, uint32_t process, uint32_t rights);

/* Enqueue a kernel-originated request, assigning its request ID and sender. */
bool ipc_kernel_send(uint32_t endpoint, const struct janos_ipc_message *message);

/* Enqueue a kernel-originated one-word notification. */
bool ipc_kernel_notify(uint32_t endpoint, uint32_t type, uint32_t value);

/* Wake a receiver waiting on an endpoint when an external event makes progress. */
void ipc_wake_receiver(uint32_t endpoint);

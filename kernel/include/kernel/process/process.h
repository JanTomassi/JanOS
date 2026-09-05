#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <list.h>
#include <kernel/phy_mem.h>
#include <arch/i386/context.h>
#include <janos/process.h>
#include <janos/syscall.h>

struct address_space;
struct process_stack;
struct wait_queue;

typedef uint32_t process_pid_t;

enum process_state {
	PROCESS_NEW,
	PROCESS_READY,
	PROCESS_RUNNING,
	PROCESS_BLOCKED,
	PROCESS_ZOMBIE,
	PROCESS_DEAD,
};

struct process;

struct process_ipc_wait {
	bool active;
	uint32_t syscall;
	uint32_t user_buffer;
	uint32_t deadline;
	uint32_t endpoint;
	struct janos_ipc_message message;
	uint32_t result;
};

#define PROCESS_CPU_UNASSIGNED 0xFFu

void process_system_init(void);
struct process *process_create(struct process *parent);
struct process *process_create_child(process_pid_t parent_pid);
void process_destroy(struct process *process);

/* Return the process currently running on this CPU, if any. */
struct process *process_current(void);
bool process_start(struct process *process, uintptr_t entry, int argc,
                   const char *const argv[]);
bool process_initial_context(const struct process *process,
                             struct i386_context *context);
[[noreturn]] void process_exit_current(int status);

process_pid_t process_pid(const struct process *process);
struct process *process_parent(const struct process *process);
struct address_space *process_address_space(const struct process *process);
const fatptr_t *process_page_directory(const struct process *process);
struct process_stack *process_kernel_stack(const struct process *process);
struct process_stack *process_user_stack(const struct process *process);
uintptr_t process_user_entry(const struct process *process);
void *process_user_stack_pointer(const struct process *process);

enum process_state process_get_state(const struct process *process);
uint8_t process_owner_cpu(const struct process *process);
uint8_t process_cpu_affinity(const struct process *process);
bool process_set_state(struct process *process, enum process_state state);
int process_exit_status(const struct process *process);
void process_exit(struct process *process, int status);
/* Reaps an exited direct child; NOHANG returns zero while it is running. */
int32_t process_wait_child(struct process *parent, process_pid_t pid,
                           uintptr_t status_address, uint32_t options);

/* Returns the child with pid, or NULL if it is not a direct child. */
struct process *process_find_child(const struct process *parent,
                                   process_pid_t pid);
bool process_exists(process_pid_t pid);
struct process *process_find_pid(process_pid_t pid);
size_t process_child_count(const struct process *parent);

/* Copy a stable, userspace-safe diagnostic record while holding process state. */
int32_t process_snapshot(size_t index, struct janos_process_info *info);
int32_t process_snapshot_pid(process_pid_t pid, struct janos_process_info *info);

/* Internal ownership hooks used by wait queues. */
bool process_block(struct process *process, struct wait_queue *queue);
bool process_wake(struct process *process, struct wait_queue *queue);
bool process_wait_detach(struct process *process, struct wait_queue *queue);
bool process_wait_requeue(struct process *process, struct wait_queue *from,
                          struct wait_queue *to);
struct process *process_from_wait_link(struct list_head *link);
void process_save_context(struct process *, const struct i386_context *);
bool process_load_context(const struct process *, struct i386_context *);
struct list_head *process_run_link(struct process *);
struct process *process_from_run_link(struct list_head *);
bool process_is_queued(const struct process *);
void process_set_queued(struct process *, bool);
bool process_set_affinity(struct process *, uint8_t);
struct wait_queue *process_waiting_queue(const struct process *);

bool process_ipc_wait_begin(struct process *, uint32_t syscall, uintptr_t user_buffer,
                            uint32_t deadline);
bool process_ipc_wait_active(const struct process *);
uint32_t process_ipc_wait_deadline(const struct process *);
uintptr_t process_ipc_wait_reply_buffer(const struct process *);
bool process_ipc_wait_set_message(struct process *, uint32_t endpoint,
                                  const struct janos_ipc_message *message);
bool process_ipc_wait_get_message(const struct process *, uint32_t *endpoint,
                                  struct janos_ipc_message *message);
bool process_ipc_wait_complete(struct process *, int32_t result,
                                const struct janos_ipc_message *message);
bool process_ipc_wait_cancel(struct process *);
bool process_ipc_wait_save_context(struct process *, const struct i386_context *);

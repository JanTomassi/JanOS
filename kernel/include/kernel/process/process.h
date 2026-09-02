#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <list.h>
#include <kernel/phy_mem.h>
#include <arch/i386/context.h>

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

#define PROCESS_CPU_UNASSIGNED 0xFFu

void process_system_init(void);
struct process *process_create(struct process *parent);
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

/* Returns the child with pid, or NULL if it is not a direct child. */
struct process *process_find_child(const struct process *parent,
                                   process_pid_t pid);
size_t process_child_count(const struct process *parent);

/* Internal ownership hooks used by wait queues. */
bool process_block(struct process *process, struct wait_queue *queue);
bool process_wake(struct process *process, struct wait_queue *queue);
struct process *process_from_wait_link(struct list_head *link);
void process_save_context(struct process *, const struct i386_context *);
bool process_load_context(const struct process *, struct i386_context *);
struct list_head *process_run_link(struct process *);
struct process *process_from_run_link(struct list_head *);
bool process_is_queued(const struct process *);
void process_set_queued(struct process *, bool);
bool process_set_affinity(struct process *, uint8_t);
struct wait_queue *process_waiting_queue(const struct process *);

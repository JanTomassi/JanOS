#pragma once

#include <stdint.h>

#include <janos/syscall.h>

#define JANOS_PROCESS_NAME_SIZE 16u
#define JANOS_PROCESS_ARGUMENT_SIZE 128u
#define JANOS_PROCESS_SPAWN_ARGUMENT_SIZE \
	(JANOS_IPC_PAYLOAD_SIZE - sizeof(uint32_t))
#define JANOS_PROCESS_CPU_ANY 0xffffffffu
#define JANOS_PROCESS_WAIT_NOHANG 1u
#define JANOS_PROCESS_PROTOCOL_VERSION 1u

enum janos_process_message_type {
	JANOS_PROCESS_MSG_LIST = 0x50520001u,
	JANOS_PROCESS_MSG_SPAWN_CALC = 0x50520002u,
};

enum janos_process_state {
	JANOS_PROCESS_NEW = 0,
	JANOS_PROCESS_READY,
	JANOS_PROCESS_RUNNING,
	JANOS_PROCESS_BLOCKED,
	JANOS_PROCESS_ZOMBIE,
	JANOS_PROCESS_DEAD,
};

struct janos_process_info {
	uint32_t pid;
	uint32_t state;
	int32_t status;
	uint32_t cpu;
	uint32_t affinity;
	uint32_t entry;
	uint32_t address_space;
	char name[JANOS_PROCESS_NAME_SIZE];
};

struct janos_process_list_request {
	uint32_t index;
};

struct janos_process_list_reply {
	int32_t status;
	struct janos_process_info process;
};

struct janos_process_spawn_request {
	uint32_t argument_length;
	char argument[JANOS_PROCESS_SPAWN_ARGUMENT_SIZE];
};

struct janos_process_spawn_reply {
	int32_t status;
	struct janos_process_info process;
	uint32_t input_endpoint;
};

struct janos_process_spawn_result {
	struct janos_process_info process;
	uint32_t input_endpoint;
};

/* This structure is consumed by the guarded process-manager syscall. */
struct janos_process_exec_request {
	uint32_t parent_pid;
	uint32_t cpu_affinity;
	uint32_t argument_length;
	char name[JANOS_PROCESS_NAME_SIZE];
	char argument[JANOS_PROCESS_ARGUMENT_SIZE];
};

_Static_assert(sizeof(struct janos_process_info) == 44,
	"process info ABI changed");
_Static_assert(sizeof(struct janos_process_list_reply) <= JANOS_IPC_PAYLOAD_SIZE,
	"process list reply exceeds IPC payload");
_Static_assert(sizeof(struct janos_process_spawn_request) == JANOS_IPC_PAYLOAD_SIZE,
	"process spawn request must fit IPC payload");
_Static_assert(sizeof(struct janos_process_spawn_reply) <= JANOS_IPC_PAYLOAD_SIZE,
	"process spawn reply exceeds IPC payload");
_Static_assert(sizeof(struct janos_process_spawn_result) <= JANOS_IPC_PAYLOAD_SIZE,
	"process spawn result exceeds IPC payload");

/**
 * Copy a process-table entry into `info`.
 *
 * `index` is a zero-based enumeration index, not a PID.  The function returns
 * zero on success, -JANOS_ENOENT when the index is outside the current table,
 * or another negative -JANOS_* value.  This syscall is reserved for the
 * authorized process-management service.
 */
int32_t janos_process_snapshot(uint32_t index, struct janos_process_info *info);

/**
 * Ask the authorized process-management service to load and start an app.
 *
 * The request supplies the parent PID, executable name, one argument string,
 * and optional CPU affinity.  On success `result` receives the new process
 * snapshot and an endpoint capability for sending it input.  The request and
 * result buffers are copied by the kernel, so they may be stack allocated.
 */
int32_t janos_process_spawn(const struct janos_process_exec_request *request,
                           struct janos_process_spawn_result *result);

/**
 * Poll and, when it has exited, reap a direct child.
 *
 * On success the child's exit status is stored in `status` when non-null and
 * the child PID is returned.  JANOS_PROCESS_WAIT_NOHANG returns zero while
 * the child is still running; without it the current kernel returns
 * -JANOS_EAGAIN instead of blocking.
 */
int32_t janos_process_wait(uint32_t pid, int32_t *status, uint32_t options);

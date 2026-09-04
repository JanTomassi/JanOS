#pragma once

#include <stdint.h>

#include <janos/syscall.h>

#define JANOS_PROCESS_NAME_SIZE 16u
#define JANOS_PROCESS_ARGUMENT_SIZE 128u
#define JANOS_PROCESS_SPAWN_ARGUMENT_SIZE \
	(JANOS_IPC_PAYLOAD_SIZE - sizeof(uint32_t))
#define JANOS_PROCESS_CPU_ANY 0xffffffffu
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

int32_t janos_process_snapshot(uint32_t index, struct janos_process_info *info);
int32_t janos_process_spawn(const struct janos_process_exec_request *request,
	                           struct janos_process_info *info);

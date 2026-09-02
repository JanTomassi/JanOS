#pragma once

#include <stdint.h>

enum janos_syscall_number {
	JANOS_SYS_EXIT = 1,
	JANOS_SYS_YIELD = 2,
	JANOS_SYS_READ = 3,
	JANOS_SYS_WRITE = 4,
	JANOS_SYS_IPC_ENDPOINT_CREATE = 5,
	JANOS_SYS_IPC_SEND = 6,
	JANOS_SYS_IPC_RECEIVE = 7,
	JANOS_SYS_IPC_REPLY = 8,
	JANOS_SYS_IPC_NOTIFY = 9,
	JANOS_SYS_MAX = 32,
};

#define JANOS_IPC_PAYLOAD_SIZE 64u
#define JANOS_IPC_QUEUE_SIZE 8u
#define JANOS_IPC_ENDPOINT_LIMIT 32u
#define JANOS_IPC_TIMEOUT_INFINITE 0xffffffffu

enum janos_ipc_message_flags {
	JANOS_IPC_REQUEST = 1u,
	JANOS_IPC_REPLY = 2u,
	JANOS_IPC_NOTIFICATION = 4u,
};

struct janos_ipc_header {
	uint32_t type;
	uint32_t flags;
	uint32_t length;
	uint32_t request_id;
	uint32_t sender;
};

struct janos_ipc_message {
	struct janos_ipc_header header;
	uint8_t payload[JANOS_IPC_PAYLOAD_SIZE];
};

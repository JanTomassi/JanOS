#pragma once

#include <stdint.h>

/*
 * Stable user/kernel syscall and IPC ABI.
 *
 * On i386 a syscall is issued with int $0x80.  The syscall number is passed in
 * eax, arguments are passed in ebx, ecx, edx, and esi (in that order), and the
 * result is returned in eax.  Successful calls return a non-negative value;
 * failures return the negated value from enum janos_error.  User pointers are
 * validated by the kernel before they are read or written.
 *
 * The libc wrappers for these calls are declared by <unistd.h>,
 * <janos/ipc.h>, and <janos/process.h>.  The numeric values below are part of
 * the ABI and must not be renumbered.
 */
enum janos_syscall_number {
	/* ebx: exit status.  Terminates the calling process and does not return. */
	JANOS_SYS_EXIT = 1,
	/* No arguments.  Gives up the processor and returns zero when scheduled. */
	JANOS_SYS_YIELD = 2,
	/* ebx: fd (0), ecx: writable buffer, edx: byte count. */
	JANOS_SYS_READ = 3,
	/* ebx: fd (1 or 2), ecx: readable buffer, edx: byte count. */
	JANOS_SYS_WRITE = 4,
	/* ebx: endpoint creation flags.  Current flags value is zero. */
	JANOS_SYS_IPC_ENDPOINT_CREATE = 5,
	/* ebx: endpoint, ecx: request message, edx: timeout in scheduler ticks. */
	JANOS_SYS_IPC_SEND = 6,
	/* ebx: endpoint, ecx: receive buffer, edx: timeout in scheduler ticks. */
	JANOS_SYS_IPC_RECEIVE = 7,
	/* ebx: endpoint, ecx: request ID, edx: reply message. */
	JANOS_SYS_IPC_REPLY = 8,
	/* ebx: endpoint, ecx: notification type, edx: 32-bit notification value. */
	JANOS_SYS_IPC_NOTIFY = 9,
	/* ebx: endpoint, ecx: target PID, edx: JANOS_IPC_RIGHT_* mask. */
	JANOS_SYS_IPC_GRANT = 10,
	/* ebx: endpoint, ecx: request ID to cancel. */
	JANOS_SYS_IPC_CANCEL = 11,
	/* ebx: endpoint.  The endpoint owner invalidates the endpoint handle. */
	JANOS_SYS_IPC_CLOSE = 12,
	/* ebx: endpoint, ecx: request, edx: reply buffer, esi: timeout. */
	JANOS_SYS_IPC_CALL = 13,
	/* ecx: writable buffer, edx: byte count (at most JANOS_FRAMEBUFFER_OUTPUT_CHUNK). */
	JANOS_SYS_FRAMEBUFFER_READ = 14,
	/* No arguments.  Returns the logical CPU index running the caller. */
	JANOS_SYS_CPU_GET = 15,
	/* ebx: process-table index, ecx: janos_process_info output buffer. */
	JANOS_SYS_PROCESS_SNAPSHOT = 16,
	/* ebx: exec request, ecx: janos_process_spawn_result output buffer. */
	JANOS_SYS_PROCESS_SPAWN = 17,
	/* ebx: child PID, ecx: status output pointer, edx: wait options. */
	JANOS_SYS_PROCESS_WAIT = 18,
	/* Exclusive upper bound for syscall dispatch slots; IDs 19 through 31 are reserved. */
	JANOS_SYS_MAX = 32,
};

/* Maximum bytes copied as the payload of one IPC message. */
#define JANOS_IPC_PAYLOAD_SIZE 64u

/* Maximum number of bytes returned by one framebuffer-output read. */
#define JANOS_FRAMEBUFFER_OUTPUT_CHUNK 1024u

/* Maximum number of messages queued by one endpoint. */
#define JANOS_IPC_QUEUE_SIZE 8u

/* Maximum number of live endpoint slots.  Handles encode a slot and generation. */
#define JANOS_IPC_ENDPOINT_LIMIT 32u

/* A timeout value meaning wait without a deadline; zero means do not wait. */
#define JANOS_IPC_TIMEOUT_INFINITE 0xffffffffu

/*
 * Error values returned as negative integers.  JanOS does not set errno for
 * these low-level wrappers, so callers compare the return value with
 * -JANOS_E* directly.
 */
enum janos_error {
	/* The file descriptor, endpoint handle, or required capability is invalid. */
	JANOS_EBADF = 9,
	/* A user-space pointer does not refer to a valid buffer of the requested size. */
	JANOS_EFAULT = 14,
	/* A process, request, or other required kernel object does not exist. */
	JANOS_ESRCH = 3,
	/* An argument, option, message flag, or message layout is invalid. */
	JANOS_EINVAL = 22,
	/* The operation would block, timed out, or could not make immediate progress. */
	JANOS_EAGAIN = 11,
	/* A bounded kernel resource, such as endpoint or capability storage, is full. */
	JANOS_ENOMEM = 12,
	/* A requested indexed object, such as a process snapshot, was not found. */
	JANOS_ENOENT = 2,
	/* A nonblocking receive found no queued message. */
	JANOS_ENOMSG = 42,
	/* The syscall number is unknown or has no registered kernel handler. */
	JANOS_ENOSYS = 38,
};

/*
 * Capabilities granted for an endpoint.  A newly created endpoint grants all
 * four rights to its owner.  Rights may be combined with bitwise OR when
 * passed to janos_ipc_grant().
 */
enum janos_ipc_rights {
	/* Permit a process to enqueue request messages. */
	JANOS_IPC_RIGHT_SEND = 1u,
	/* Permit a process to dequeue messages from the endpoint. */
	JANOS_IPC_RIGHT_RECEIVE = 2u,
	/* Permit a process to complete queued requests with replies. */
	JANOS_IPC_RIGHT_REPLY = 4u,
	/* Permit a process to enqueue one-word notification messages. */
	JANOS_IPC_RIGHT_NOTIFY = 8u,
};

/* Exactly one message kind must be selected in every valid message. */
enum janos_ipc_message_flags {
	/* A request expects the endpoint owner to process it and optionally reply. */
	JANOS_IPC_REQUEST = 1u,
	/* A response to a request; header.request_id must identify that request. */
	JANOS_IPC_REPLY = 2u,
	/* An asynchronous event carrying a type and usually a small payload. */
	JANOS_IPC_NOTIFICATION = 4u,
};

/*
 * The fixed 20-byte header at the beginning of every IPC message.
 *
 * type is selected by the application-level protocol.  flags identifies the
 * message kind and must contain exactly one janos_ipc_message_flags value.
 * length is the number of meaningful bytes in payload and may not exceed
 * JANOS_IPC_PAYLOAD_SIZE.  The kernel assigns request_id when a request or
 * notification is queued; a reply must copy the request ID it answers.
 * sender is the kernel-assigned sender PID for user requests and notifications
 * (kernel-generated messages use zero), so a caller must not use a supplied
 * sender value to impersonate another process.
 */
struct janos_ipc_header {
	/* Application-defined operation or event identifier. */
	uint32_t type;
	/* One and only one of JANOS_IPC_REQUEST, JANOS_IPC_REPLY, or NOTIFICATION. */
	uint32_t flags;
	/* Number of valid bytes in payload, from zero through 64. */
	uint32_t length;
	/* Kernel-generated correlation token; replies must echo the request token. */
	uint32_t request_id;
	/* Kernel-provided sender PID for received user messages. */
	uint32_t sender;
};

/*
 * Fixed-size IPC object exchanged with the kernel.  Only the first `length`
 * bytes of payload are meaningful; the complete object is copied for every
 * syscall, so callers may use ordinary stack-allocated messages.
 */
struct janos_ipc_message {
	struct janos_ipc_header header;
	/* Protocol payload; its contents are defined by header.type. */
	uint8_t payload[JANOS_IPC_PAYLOAD_SIZE];
};

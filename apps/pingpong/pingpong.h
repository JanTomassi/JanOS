#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <janos/syscall.h>

#define PINGPONG_MESSAGE_TYPE 0x50494e47u

static inline struct janos_ipc_message pingpong_ping(uint32_t sequence)
{
	struct janos_ipc_message message = { .header = {
		.type = PINGPONG_MESSAGE_TYPE,
		.flags = JANOS_IPC_REQUEST,
		.length = sizeof(sequence),
	}};
	message.payload[0] = (uint8_t)sequence;
	message.payload[1] = (uint8_t)(sequence >> 8);
	message.payload[2] = (uint8_t)(sequence >> 16);
	message.payload[3] = (uint8_t)(sequence >> 24);
	return message;
}

static inline uint32_t pingpong_sequence(const struct janos_ipc_message *message)
{
	return (uint32_t)message->payload[0] |
		((uint32_t)message->payload[1] << 8) |
		((uint32_t)message->payload[2] << 16) |
		((uint32_t)message->payload[3] << 24);
}

static inline bool pingpong_is_ping(const struct janos_ipc_message *message)
{
	return message != 0 && message->header.type == PINGPONG_MESSAGE_TYPE &&
		message->header.flags == JANOS_IPC_REQUEST &&
		message->header.length == sizeof(uint32_t);
}

static inline struct janos_ipc_message pingpong_pong(
	const struct janos_ipc_message *request)
{
	struct janos_ipc_message reply = *request;
	reply.header.flags = JANOS_IPC_REPLY;
	return reply;
}

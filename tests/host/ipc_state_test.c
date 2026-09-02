#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <janos/syscall.h>

struct queue {
	struct janos_ipc_message messages[JANOS_IPC_QUEUE_SIZE];
	size_t head;
	size_t count;
};

static bool valid(const struct janos_ipc_message *message)
{
	return message != 0 && message->header.length <= JANOS_IPC_PAYLOAD_SIZE &&
		message->header.flags != 0 &&
		(message->header.flags & ~(JANOS_IPC_REQUEST | JANOS_IPC_REPLY |
		JANOS_IPC_NOTIFICATION)) == 0;
}

static bool valid_handle(uint32_t handle)
{
	uint32_t slot = (handle & 0xffffu) - 1u;
	return (handle >> 16) != 0 && slot < JANOS_IPC_ENDPOINT_LIMIT;
}

static bool push(struct queue *queue, const struct janos_ipc_message *message)
{
	if (!valid(message) || queue->count == JANOS_IPC_QUEUE_SIZE)
		return false;
	queue->messages[(queue->head + queue->count) % JANOS_IPC_QUEUE_SIZE] = *message;
	++queue->count;
	return true;
}

static bool pop(struct queue *queue, struct janos_ipc_message *message)
{
	if (queue->count == 0 || message == 0)
		return false;
	*message = queue->messages[queue->head];
	queue->head = (queue->head + 1) % JANOS_IPC_QUEUE_SIZE;
	--queue->count;
	return true;
}

int main(void)
{
	_Static_assert(sizeof(struct janos_ipc_header) == 20, "IPC header ABI changed");
	_Static_assert(sizeof(struct janos_ipc_message) == 84, "IPC message ABI changed");
	struct queue queue = { 0 };
	assert(valid_handle(0x00010001u));
	assert(valid_handle(0x12340020u));
	assert(!valid_handle(0));
	assert(!valid_handle(0x00010021u));
	assert(!valid_handle(0x00000001u));
	struct janos_ipc_message message = { .header = {
		.type = 7, .flags = JANOS_IPC_REQUEST, .length = 3, .request_id = 1,
	}, .payload = { 'e', 'c', 'h' } };
	assert(valid(&message));
	for (size_t i = 0; i < JANOS_IPC_QUEUE_SIZE; ++i)
		assert(push(&queue, &message));
	assert(!push(&queue, &message));
	message.header.length = JANOS_IPC_PAYLOAD_SIZE + 1;
	assert(!valid(&message));
	message.header.length = 3;
	message.header.flags = 0x80;
	assert(!valid(&message));
	struct janos_ipc_message received;
	message.header.flags = JANOS_IPC_REQUEST;
	assert(pop(&queue, &received));
	assert(received.header.request_id == 1 && received.payload[0] == 'e');
	return 0;
}

#include <assert.h>
#include <stdint.h>

#include "../../apps/pingpong/pingpong.h"

int main(void)
{
	for (uint32_t sequence = 0; sequence < 1000; ++sequence) {
		struct janos_ipc_message request = pingpong_ping(sequence);
		assert(pingpong_is_ping(&request));
		assert(pingpong_sequence(&request) == sequence);
		struct janos_ipc_message reply = pingpong_pong(&request);
		assert(reply.header.flags == JANOS_IPC_REPLY);
		assert(reply.header.request_id == request.header.request_id);
		assert(pingpong_sequence(&reply) == sequence);
	}
	struct janos_ipc_message malformed = pingpong_ping(1);
	malformed.header.flags |= JANOS_IPC_REPLY;
	assert(!pingpong_is_ping(&malformed));
	return 0;
}

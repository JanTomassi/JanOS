#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <janos/framebuffer.h>

static struct janos_ipc_message request(uint32_t type, const void *payload,
	uint32_t length)
{
	struct janos_ipc_message message = { .header = {
		.type = type,
		.flags = JANOS_IPC_REQUEST,
		.length = length,
	} };
	if (payload != NULL)
		memcpy(message.payload, payload, length);
	return message;
}

int main(void)
{
	_Static_assert(sizeof(struct janos_fb_putc) <= JANOS_IPC_PAYLOAD_SIZE,
		"putc exceeds IPC payload");
	_Static_assert(sizeof(struct janos_fb_cursor) <= JANOS_IPC_PAYLOAD_SIZE,
		"cursor exceeds IPC payload");
	_Static_assert(sizeof(struct janos_fb_scroll) <= JANOS_IPC_PAYLOAD_SIZE,
		"scroll exceeds IPC payload");
	_Static_assert(sizeof(struct janos_fb_puts) == JANOS_IPC_PAYLOAD_SIZE,
		"puts ABI changed");
	struct janos_fb_putc putc = { .value = 'A' };
	struct janos_ipc_message message = request(JANOS_FB_MSG_PUTC, &putc,
		sizeof(putc));
	assert(message.header.flags == JANOS_IPC_REQUEST);
	assert(message.header.type == JANOS_FB_MSG_PUTC);
	assert(message.header.length == sizeof(putc));
	assert(message.payload[0] == 'A');
	assert(JANOS_FB_MSG_PUTC != JANOS_FB_MSG_PUTS);
	assert(JANOS_FB_MSG_PUTS != JANOS_FB_MSG_CURSOR);
	assert(JANOS_FB_MSG_CURSOR != JANOS_FB_MSG_CLEAR);
	assert(JANOS_FB_MSG_CLEAR != JANOS_FB_MSG_SCROLL);
	assert(-JANOS_EBADF == -9);
	assert(JANOS_FB_STATUS_OK == 0);
	return 0;
}

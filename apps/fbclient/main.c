#include <janos/framebuffer.h>
#include <janos/ipc.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void print(const char *text)
{
	(void)write(1, text, strlen(text));
}

static void print_uint(uint32_t value)
{
	char buffer[10];
	size_t count = 0;
	if (value == 0)
		buffer[count++] = '0';
	while (value != 0) {
		buffer[count++] = (char)('0' + value % 10);
		value /= 10;
	}
	for (size_t i = count; i != 0; --i)
		(void)write(1, &buffer[i - 1], 1);
}

static struct janos_ipc_message request(uint32_t type, const void *payload,
	size_t length)
{
	struct janos_ipc_message message = { .header = {
		.type = type,
		.flags = JANOS_IPC_REQUEST,
		.length = (uint32_t)length,
	} };
	if (payload != 0 && length != 0)
		memcpy(message.payload, payload, length);
	return message;
}

static int32_t call(uint32_t endpoint, uint32_t type, const void *payload,
	size_t length)
{
	struct janos_ipc_message request_message = request(type, payload, length);
	struct janos_ipc_message response;
	int32_t result = janos_ipc_call(endpoint, &request_message, &response, 100);
	if (result < 0)
		return result;
	if (response.header.type != type || response.header.flags != JANOS_IPC_REPLY ||
		response.header.length != sizeof(struct janos_fb_reply))
		return -JANOS_EFAULT;
	struct janos_fb_reply reply;
	memcpy(&reply, response.payload, sizeof(reply));
	return reply.status;
}

static int32_t putc_call(uint32_t endpoint, uint32_t value)
{
	struct janos_fb_putc operation = { .value = value };
	return call(endpoint, JANOS_FB_MSG_PUTC, &operation, sizeof(operation));
}

static int32_t puts_call(uint32_t endpoint, const char *text)
{
	struct janos_fb_puts operation = { 0 };
	operation.length = (uint32_t)strlen(text);
	if (operation.length > JANOS_FB_PUTS_MAX)
		return -JANOS_EINVAL;
	memcpy(operation.text, text, operation.length);
	return call(endpoint, JANOS_FB_MSG_PUTS, &operation,
		sizeof(operation.length) + operation.length);
}

static int32_t cursor_call(uint32_t endpoint, uint32_t column, uint32_t row)
{
	struct janos_fb_cursor operation = { .column = column, .row = row };
	return call(endpoint, JANOS_FB_MSG_CURSOR, &operation, sizeof(operation));
}

static int32_t clear_call(uint32_t endpoint)
{
	return call(endpoint, JANOS_FB_MSG_CLEAR, 0, 0);
}

static int32_t scroll_call(uint32_t endpoint, uint32_t rows)
{
	struct janos_fb_scroll operation = { .rows = rows };
	return call(endpoint, JANOS_FB_MSG_SCROLL, &operation, sizeof(operation));
}

static void fail(int32_t status)
{
	print("FBCLIENT_FAIL status=");
	print_uint((uint32_t)-status);
	print("\n");
	_Exit(1);
}

static int unavailable(void)
{
	struct janos_fb_putc operation = { .value = 'x' };
	int32_t result = call(0, JANOS_FB_MSG_PUTC, &operation, sizeof(operation));
	if (result != -JANOS_EBADF)
		fail(result < 0 ? result : -JANOS_EFAULT);
	print("FBCLIENT_UNAVAILABLE_PASS\n");
	return 0;
}

static int client(int argc, char **argv)
{
	if (argc >= 2 && argv[1][0] == 'u' && argv[1][1] == 'n')
		return unavailable();
	if (argc < 3) {
		print("fbclient: endpoint missing\n");
		return 1;
	}
	uint32_t endpoint = 0;
	const char *text = argv[2];
	bool test_mode = argc >= 4 && argv[3][0] == 't' && argv[3][1] == 'e';
	while (*text >= '0' && *text <= '9') {
		endpoint = endpoint * 10u + (uint32_t)(*text - '0');
		++text;
	}
	int32_t result;
	if (test_mode) {
		result = clear_call(endpoint);
		if (result != JANOS_FB_STATUS_OK)
			fail(result);
	}
	result = cursor_call(endpoint, 1, 1);
	if (result != JANOS_FB_STATUS_OK)
		fail(result);
	result = puts_call(endpoint, "JanOS framebuffer IPC\n");
	if (result != JANOS_FB_STATUS_OK)
		fail(result);
	result = putc_call(endpoint, '!');
	if (result != JANOS_FB_STATUS_OK)
		fail(result);
	result = cursor_call(endpoint, 2, 2);
	if (result != JANOS_FB_STATUS_OK)
		fail(result);
	result = puts_call(endpoint, "visible text\n");
	if (result != JANOS_FB_STATUS_OK)
		fail(result);
	if (test_mode) {
		result = scroll_call(endpoint, 1);
		if (result != JANOS_FB_STATUS_OK)
			fail(result);
	}
	result = puts_call(endpoint, "Stage 4 PASS\n");
	if (result != JANOS_FB_STATUS_OK)
		fail(result);
	print("FBCLIENT_PASS\n");
	return 0;
}

int main(int argc, char **argv)
{
	return client(argc, argv);
}

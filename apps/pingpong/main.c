#include "pingpong.h"

#include <stdint.h>
#include <unistd.h>

static void print(const char *text)
{
	const char *end = text;
	while (*end != '\0')
		++end;
	(void)write(1, text, (size_t)(end - text));
}

static void print_uint(uint32_t value)
{
	char buffer[11];
	size_t position = sizeof(buffer);
	if (value == 0)
		buffer[--position] = '0';
	while (value != 0) {
		buffer[--position] = (char)('0' + value % 10);
		value /= 10;
	}
	(void)write(1, buffer + position, sizeof(buffer) - position);
}

static size_t append_uint(char *buffer, size_t position, uint32_t value)
{
	char digits[10];
	size_t count = 0;
	if (value == 0)
		digits[count++] = '0';
	while (value != 0) {
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	}
	while (count != 0)
		buffer[position++] = digits[--count];
	return position;
}

static void print_pong(uint32_t client_id, uint32_t sequence)
{
	char buffer[32];
	size_t position = 0;
	buffer[position++] = 'p';
	buffer[position++] = 'o';
	buffer[position++] = 'n';
	buffer[position++] = 'g';
	buffer[position++] = ' ';
	position = append_uint(buffer, position, client_id);
	buffer[position++] = ' ';
	position = append_uint(buffer, position, sequence);
	buffer[position++] = '\n';
	(void)write(1, buffer, position);
}

static uint32_t parse_uint(const char *text)
{
	uint32_t value = 0;
	while (*text >= '0' && *text <= '9') {
		value = value * 10u + (uint32_t)(*text - '0');
		++text;
	}
	return value;
}

static int server_with_endpoint(int argc, char **argv)
{
	if (argc < 3) {
		print("pingpong: endpoint capability missing\n");
		return 1;
	}
	uint32_t endpoint = parse_uint(argv[2]);
	for (;;) {
		struct janos_ipc_message request;
		int32_t request_id = janos_ipc_receive((uint32_t)endpoint, &request,
			JANOS_IPC_TIMEOUT_INFINITE);
		if (request_id < 0) {
			print("pingpong: receive failed result ");
			print_uint((uint32_t)-request_id);
			print("\n");
			return 1;
		}
		if (!pingpong_is_ping(&request))
			continue;
		struct janos_ipc_message reply = pingpong_pong(&request);
		(void)janos_ipc_reply((uint32_t)endpoint, request.header.request_id, &reply);
	}
}

static int client(int argc, char **argv)
{
	if (argc < 3) {
		print("usage: pingpong client <endpoint> [count]\n");
		return 2;
	}
	uint32_t endpoint = parse_uint(argv[2]);
	uint32_t count = argc > 3 ? parse_uint(argv[3]) : 10;
	uint32_t client_id = argc > 4 ? parse_uint(argv[4]) : 0;
	for (uint32_t sequence = 0; sequence < count; ++sequence) {
		struct janos_ipc_message ping = pingpong_ping(sequence);
		int32_t send_result = janos_ipc_send(endpoint, &ping, 100);
		if (send_result < 0) {
			print("pingpong: ping failed at sequence ");
			print_uint(sequence);
			print(" result ");
			print_uint((uint32_t)-send_result);
			print("\n");
			return 1;
		}
		print_pong(client_id, sequence);
	}
	print("PINGPONG_CLIENT_OK\n");
	/* Keep the process runnable for the current boot-time scheduler. */
	for (;;) {
		(void)sched_yield();
	}
}

int main(int argc, char **argv)
{
	if (argc >= 2 && argv[1][0] == 's' && argv[1][1] == 'e')
		return server_with_endpoint(argc, argv);
	if (argc >= 2 && argv[1][0] == 'c' && argv[1][1] == 'l')
		return client(argc, argv);
	print("usage: pingpong server | client <endpoint> [count]\n");
	return 2;
}

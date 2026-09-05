#include <janos/ipc.h>
#include <janos/process.h>

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
	char buffer[11];
	size_t position = sizeof(buffer);
	buffer[--position] = '\0';
	if (value == 0)
		buffer[--position] = '0';
	while (value != 0) {
		buffer[--position] = (char)('0' + value % 10);
		value /= 10;
	}
	print(buffer + position);
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

static void reply(uint32_t endpoint, const struct janos_ipc_message *request,
	const void *payload, size_t length)
{
	struct janos_ipc_message response = { .header = {
		.type = request->header.type,
		.flags = JANOS_IPC_REPLY,
		.length = (uint32_t)length,
		.request_id = request->header.request_id,
	} };
	memcpy(response.payload, payload, length);
	(void)janos_ipc_reply(endpoint, request->header.request_id, &response);
}

static void reply_error(uint32_t endpoint, const struct janos_ipc_message *request,
	int32_t status)
{
	if (request->header.type == JANOS_PROCESS_MSG_LIST) {
		struct janos_process_list_reply response = { .status = status };
		reply(endpoint, request, &response, sizeof(response));
	} else {
		struct janos_process_spawn_reply response = { .status = status };
		reply(endpoint, request, &response, sizeof(response));
	}
}

static int32_t handle_request(uint32_t endpoint,
	const struct janos_ipc_message *request)
{
	if (request->header.type == JANOS_PROCESS_MSG_LIST) {
		if (request->header.length != sizeof(struct janos_process_list_request))
			return -JANOS_EINVAL;
		struct janos_process_list_request operation;
		memcpy(&operation, request->payload, sizeof(operation));
		struct janos_process_list_reply response = { 0 };
		response.status = janos_process_snapshot(operation.index, &response.process);
		reply(endpoint, request, &response, sizeof(response));
		return 0;
	}
	if (request->header.type == JANOS_PROCESS_MSG_SPAWN_CALC) {
		if (request->header.length != sizeof(struct janos_process_spawn_request))
			return -JANOS_EINVAL;
		struct janos_process_spawn_request operation;
		memcpy(&operation, request->payload, sizeof(operation));
		if (operation.argument_length >= sizeof(operation.argument))
			return -JANOS_EINVAL;
		struct janos_process_exec_request spawn = {
			.parent_pid = request->header.sender,
			.cpu_affinity = JANOS_PROCESS_CPU_ANY,
			.argument_length = operation.argument_length,
		};
		memcpy(spawn.name, "calc", sizeof("calc"));
		memcpy(spawn.argument, operation.argument, operation.argument_length);
		struct janos_process_spawn_result result = { 0 };
		struct janos_process_spawn_reply response = { 0 };
		response.status = janos_process_spawn(&spawn, &result);
		response.process = result.process;
		response.input_endpoint = result.input_endpoint;
		reply(endpoint, request, &response, sizeof(response));
		return 0;
	}
	return -JANOS_EINVAL;
}

static int server(int argc, char **argv)
{
	if (argc < 3) {
		print("procserv: endpoint missing\n");
		return 1;
	}
	uint32_t endpoint = parse_uint(argv[2]);
	print("PROCSERV_READY cpu=");
	print_uint((uint32_t)janos_cpu_get());
	print("\n");
	for (;;) {
		struct janos_ipc_message request;
		int32_t result = janos_ipc_receive(endpoint, &request,
			JANOS_IPC_TIMEOUT_INFINITE);
		if (result < 0) {
			if (result == -JANOS_EAGAIN || result == -JANOS_ENOMSG)
				continue;
			return 1;
		}
		result = handle_request(endpoint, &request);
		if (result < 0)
			reply_error(endpoint, &request, result);
	}
}

int main(int argc, char **argv)
{
	if (argc >= 2 && argv[1][0] == 's' && argv[1][1] == 'e')
		return server(argc, argv);
	print("usage: procserv server <endpoint>\n");
	return 1;
}

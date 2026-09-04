#include <janos/framebuffer.h>
#include <janos/input.h>
#include <janos/ipc.h>
#include <janos/process.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define SHELL_LINE_SIZE 128u
#define SHELL_HISTORY_SIZE 8u

struct shell_state {
	uint32_t endpoint;
	uint32_t framebuffer_endpoint;
	uint32_t process_endpoint;
	uint32_t input_endpoint;
	char line[SHELL_LINE_SIZE];
	size_t line_length;
	char history[SHELL_HISTORY_SIZE][SHELL_LINE_SIZE];
	size_t history_count;
	int32_t history_cursor;
};

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

static bool command_is(const char *line, const char *command)
{
	size_t position = 0;
	while (command[position] != '\0' && line[position] == command[position])
		++position;
	return command[position] == '\0' &&
		(line[position] == '\0' || line[position] == ' ' || line[position] == '\t');
}

static const char *command_argument(const char *line, const char *command)
{
	const char *argument = line + strlen(command);
	while (*argument == ' ' || *argument == '\t')
		++argument;
	return argument;
}

static void print_process(const struct janos_process_info *process)
{
	print("pid=");
	print_uint(process->pid);
	print(" name=");
	print(process->name);
	print(" state=");
	switch (process->state) {
	case JANOS_PROCESS_NEW: print("new"); break;
	case JANOS_PROCESS_READY: print("ready"); break;
	case JANOS_PROCESS_RUNNING: print("running"); break;
	case JANOS_PROCESS_BLOCKED: print("blocked"); break;
	case JANOS_PROCESS_ZOMBIE: print("zombie"); break;
	case JANOS_PROCESS_DEAD: print("dead"); break;
	default: print("unknown"); break;
	}
	print(" cpu=");
	print_uint(process->cpu);
	print(" affinity=");
	if (process->affinity == 0xffu)
		print("any");
	else
		print_uint(process->affinity);
	print(" entry=");
	print_uint(process->entry);
	print(" address-space=");
	print_uint(process->address_space);
}

static int32_t framebuffer_call(const struct shell_state *state, uint32_t type,
	const void *payload, size_t length, struct janos_ipc_message *response)
{
	if (state->framebuffer_endpoint == 0)
		return -JANOS_EBADF;
	struct janos_ipc_message request = { .header = {
		.type = type,
		.flags = JANOS_IPC_REQUEST,
		.length = (uint32_t)length,
	} };
	if (payload != nullptr && length != 0)
		memcpy(request.payload, payload, length);
	return janos_ipc_call(state->framebuffer_endpoint, &request, response, 100);
}

static int32_t framebuffer_status(const struct shell_state *state, uint32_t type,
	const void *payload, size_t length)
{
	struct janos_ipc_message response;
	int32_t result = framebuffer_call(state, type, payload, length, &response);
	if (result < 0)
		return result;
	if (response.header.type != type || response.header.flags != JANOS_IPC_REPLY ||
		response.header.length != sizeof(struct janos_fb_reply))
		return -JANOS_EFAULT;
	struct janos_fb_reply status;
	memcpy(&status, response.payload, sizeof(status));
	return status.status;
}

static int32_t framebuffer_info(const struct shell_state *state,
	struct janos_fb_info_reply *info)
{
	struct janos_ipc_message response;
	int32_t result = framebuffer_call(state, JANOS_FB_MSG_INFO, nullptr, 0,
		&response);
	if (result < 0)
		return result;
	if (response.header.type != JANOS_FB_MSG_INFO ||
		response.header.flags != JANOS_IPC_REPLY ||
		response.header.length != sizeof(*info))
		return -JANOS_EFAULT;
	memcpy(info, response.payload, sizeof(*info));
	return info->status;
}

static int32_t process_list(const struct shell_state *state, uint32_t index,
	struct janos_process_list_reply *reply)
{
	if (state->process_endpoint == 0)
		return -JANOS_EBADF;
	struct janos_process_list_request operation = { .index = index };
	struct janos_ipc_message request = { .header = {
		.type = JANOS_PROCESS_MSG_LIST,
		.flags = JANOS_IPC_REQUEST,
		.length = sizeof(operation),
	} };
	memcpy(request.payload, &operation, sizeof(operation));
	struct janos_ipc_message response;
	int32_t result = janos_ipc_call(state->process_endpoint, &request,
		&response, 100);
	if (result < 0)
		return result;
	if (response.header.type != JANOS_PROCESS_MSG_LIST ||
		response.header.flags != JANOS_IPC_REPLY ||
		response.header.length != sizeof(*reply))
		return -JANOS_EFAULT;
	memcpy(reply, response.payload, sizeof(*reply));
	return 0;
}

static int32_t spawn_calc(const struct shell_state *state, const char *argument,
	struct janos_process_info *process)
{
	if (state->process_endpoint == 0 || argument == nullptr)
		return -JANOS_EBADF;
	size_t length = strlen(argument);
	if (length >= JANOS_PROCESS_SPAWN_ARGUMENT_SIZE)
		return -JANOS_EINVAL;
	struct janos_process_spawn_request operation = { .argument_length = length };
	memcpy(operation.argument, argument, length);
	struct janos_ipc_message request = { .header = {
		.type = JANOS_PROCESS_MSG_SPAWN_CALC,
		.flags = JANOS_IPC_REQUEST,
		.length = sizeof(operation),
	} };
	memcpy(request.payload, &operation, sizeof(operation));
	struct janos_ipc_message response;
	int32_t result = janos_ipc_call(state->process_endpoint, &request,
		&response, 100);
	if (result < 0)
		return result;
	if (response.header.type != JANOS_PROCESS_MSG_SPAWN_CALC ||
		response.header.flags != JANOS_IPC_REPLY ||
		response.header.length != sizeof(struct janos_process_spawn_reply))
		return -JANOS_EFAULT;
	struct janos_process_spawn_reply reply;
	memcpy(&reply, response.payload, sizeof(reply));
	if (reply.status < 0)
		return reply.status;
	*process = reply.process;
	return 0;
}

static void print_process_list(const struct shell_state *state)
{
	for (uint32_t index = 0;; ++index) {
		struct janos_process_list_reply reply;
		int32_t result = process_list(state, index, &reply);
		if (result < 0) {
			print("ps: IPC error ");
			print_uint((uint32_t)-result);
			print("\n");
			return;
		}
		if (reply.status == -JANOS_ENOENT)
			return;
		if (reply.status < 0) {
			print("ps: service error ");
			print_uint((uint32_t)-reply.status);
			print("\n");
			return;
		}
		print_process(&reply.process);
		print("\n");
	}
}

static void print_framebuffer_info(const struct janos_fb_info_reply *info)
{
	print("framebuffer=");
	print_uint(info->width);
	print("x");
	print_uint(info->height);
	print(" pitch=");
	print_uint(info->pitch);
	print(" bpp=");
	print_uint(info->bpp);
	print(" type=");
	print_uint(info->type);
	print(" cells=");
	print_uint(info->columns);
	print("x");
	print_uint(info->rows);
	print("\n");
}

static void print_font_info(const struct janos_fb_info_reply *info)
{
	print("font size=");
	print_uint(info->font_size);
	print(" header=");
	print_uint(info->font_header_size);
	print(" glyphs=");
	print_uint(info->font_glyph_count);
	print(" glyph-size=");
	print_uint(info->font_glyph_size);
	print(" cell=");
	print_uint(info->font_width);
	print("x");
	print_uint(info->font_height);
	print("\n");
}

static void command_info(const struct shell_state *state)
{
	print("capabilities: shell=");
	print_uint(state->endpoint);
	print(" input=");
	print_uint(state->input_endpoint);
	print(" process=");
	print_uint(state->process_endpoint);
	print(" framebuffer=");
	print_uint(state->framebuffer_endpoint);
	print(" cpu=");
	print_uint((uint32_t)janos_cpu_get());
	print("\nservice affinity:\n");
	print_process_list(state);
	struct janos_fb_info_reply info;
	int32_t result = framebuffer_info(state, &info);
	if (result != JANOS_FB_STATUS_OK) {
		print("framebuffer info unavailable\n");
		return;
	}
	print_framebuffer_info(&info);
	print_font_info(&info);
}

static void command_font(const struct shell_state *state)
{
	struct janos_fb_info_reply info;
	int32_t result = framebuffer_info(state, &info);
	if (result != JANOS_FB_STATUS_OK) {
		print("font: framebuffer service unavailable\n");
		return;
	}
	print_font_info(&info);
}

static void history_add(struct shell_state *state)
{
	if (state->line_length == 0)
		return;
	if (state->history_count == SHELL_HISTORY_SIZE) {
		for (size_t i = 1; i < SHELL_HISTORY_SIZE; ++i)
			memcpy(state->history[i - 1], state->history[i], SHELL_LINE_SIZE);
		--state->history_count;
	}
	memcpy(state->history[state->history_count], state->line,
		state->line_length + 1);
	++state->history_count;
}

static void erase_line(const struct shell_state *state)
{
	for (size_t i = 0; i < state->line_length; ++i)
		print("\b \b");
}

static void replace_line(struct shell_state *state, const char *line)
{
	erase_line(state);
	state->line_length = strlen(line);
	if (state->line_length >= SHELL_LINE_SIZE)
		state->line_length = SHELL_LINE_SIZE - 1;
	memcpy(state->line, line, state->line_length);
	state->line[state->line_length] = '\0';
	(void)write(1, state->line, state->line_length);
}

static void history_up(struct shell_state *state)
{
	if (state->history_count == 0)
		return;
	if (state->history_cursor < (int32_t)state->history_count - 1)
		++state->history_cursor;
	size_t index = state->history_count - 1 - (size_t)state->history_cursor;
	replace_line(state, state->history[index]);
}

static void history_down(struct shell_state *state)
{
	if (state->history_cursor < 0)
		return;
	if (state->history_cursor == 0) {
		state->history_cursor = -1;
		replace_line(state, "");
		return;
	}
	--state->history_cursor;
	size_t index = state->history_count - 1 - (size_t)state->history_cursor;
	replace_line(state, state->history[index]);
}

static bool dispatch(struct shell_state *state)
{
	if (command_is(state->line, "help")) {
		print("help ps info font calc clear exit\n");
		return true;
	}
	if (command_is(state->line, "ps")) {
		print_process_list(state);
		return true;
	}
	if (command_is(state->line, "info")) {
		command_info(state);
		return true;
	}
	if (command_is(state->line, "font")) {
		command_font(state);
		return true;
	}
	if (command_is(state->line, "clear")) {
		int32_t result = framebuffer_status(state, JANOS_FB_MSG_CLEAR, nullptr, 0);
		if (result != JANOS_FB_STATUS_OK)
			print("clear: framebuffer service unavailable\n");
		return true;
	}
	if (command_is(state->line, "calc")) {
		const char *argument = command_argument(state->line, "calc");
		struct janos_process_info process;
		int32_t result = spawn_calc(state, argument, &process);
		if (result < 0) {
			print("calc: spawn failed ");
			print_uint((uint32_t)-result);
			print("\n");
		} else {
			print("calc started via IPC ");
			print_process(&process);
			print("\n");
		}
		return true;
	}
	if (command_is(state->line, "exit"))
		_Exit(0);
	print("unknown command\n");
	return true;
}

static void start_calc(struct shell_state *state)
{
	struct janos_process_info process;
	int32_t result = spawn_calc(state, "", &process);
	if (result < 0) {
		print("shell: calc startup failed ");
		print_uint((uint32_t)-result);
		print("\n");
		return;
	}
	print("shell: calc started via IPC ");
	print_process(&process);
	print("\n");
}

static void handle_key(struct shell_state *state, uint32_t event)
{
	if (!janos_input_event_pressed(event))
		return;
	uint32_t key = janos_input_event_key(event);
	uint32_t character = janos_input_event_character(event);
	if ((event & JANOS_INPUT_EVENT_CTRL) != 0 &&
		(key == JANOS_INPUT_KEY_CHARACTER &&
		(character == 'c' || character == 'C'))) {
		state->line_length = 0;
		state->line[0] = '\0';
		print("^C\njan> ");
		return;
	}
	if (key == JANOS_INPUT_KEY_CHARACTER && character >= 0x20u &&
		character < 0x7fu) {
		if (state->line_length + 1 < SHELL_LINE_SIZE) {
			state->line[state->line_length++] = (char)character;
			state->line[state->line_length] = '\0';
			(void)write(1, &character, 1);
		}
		return;
	}
	if (key == JANOS_INPUT_KEY_BACKSPACE) {
		if (state->line_length != 0) {
			--state->line_length;
			state->line[state->line_length] = '\0';
			print("\b \b");
		}
		return;
	}
	if (key == JANOS_INPUT_KEY_ARROW_UP) {
		history_up(state);
		return;
	}
	if (key == JANOS_INPUT_KEY_ARROW_DOWN) {
		history_down(state);
		return;
	}
	if (key == JANOS_INPUT_KEY_ENTER) {
		print("\n");
		history_add(state);
		(void)dispatch(state);
		state->line_length = 0;
		state->line[0] = '\0';
		state->history_cursor = -1;
		print("jan> ");
	}
}

static int server(int argc, char **argv)
{
	if (argc < 6) {
		print("shell: endpoint capabilities missing\n");
		return 1;
	}
	struct shell_state state = {
		.endpoint = parse_uint(argv[2]),
		.framebuffer_endpoint = parse_uint(argv[3]),
		.process_endpoint = parse_uint(argv[4]),
		.input_endpoint = parse_uint(argv[5]),
		.history_cursor = -1,
	};
	print("SHELL_READY cpu=");
	print_uint((uint32_t)janos_cpu_get());
	print("\n");
	start_calc(&state);
	print("jan> ");
	for (;;) {
		struct janos_ipc_message message;
		int32_t result = janos_ipc_receive(state.endpoint, &message,
			JANOS_IPC_TIMEOUT_INFINITE);
		if (result < 0) {
			if (result == -JANOS_EAGAIN || result == -JANOS_ENOMSG)
				continue;
			return 1;
		}
		if (message.header.type != JANOS_INPUT_MSG_KEY ||
			message.header.flags != JANOS_IPC_NOTIFICATION ||
			message.header.length != sizeof(uint32_t))
			continue;
		uint32_t event;
		memcpy(&event, message.payload, sizeof(event));
		handle_key(&state, event);
	}
}

int main(int argc, char **argv)
{
	if (argc >= 2 && argv[1][0] == 's' && argv[1][1] == 'e')
		return server(argc, argv);
	print("usage: shell server <endpoint> <framebuffer> <process> <input>\n");
	return 1;
}

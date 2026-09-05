#include "calc.h"
#include <janos/input.h>
#include <janos/ipc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void print(const char *text)
{
	(void)write(1, text, strlen(text));
}

static void print_integer(calc_int32_t value)
{
	char buffer[12];
	calc_size_t position = sizeof(buffer);
	calc_uint32_t magnitude = value < 0 ? (calc_uint32_t)(-(long long)value) : (calc_uint32_t)value;
	if (magnitude == 0)
		buffer[--position] = '0';
	while (magnitude != 0) {
		buffer[--position] = (char)('0' + magnitude % 10);
		magnitude /= 10;
	}
	if (value < 0)
		buffer[--position] = '-';
	(void)write(1, buffer + position, sizeof(buffer) - position);
}

static int command_is(const char *line, const char *command)
{
	calc_size_t i = 0;
	while (command[i] != '\0' && line[i] == command[i])
		++i;
	return command[i] == '\0' && (line[i] == '\0' || line[i] == ' ' || line[i] == '\t');
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

static int evaluate(const char *expression)
{
	calc_int32_t result;
	enum calc_status status = calc_eval(expression, (calc_size_t)strlen(expression), &result);
	if (status == CALC_OK) {
		print_integer(result);
		print("\n");
	} else {
		print("error: ");
		print(calc_status_string(status));
		print("\n");
	}
	return status == CALC_OK ? 0 : 1;
}

static bool handle_line(char *input, size_t *length, char *last_input,
	bool *has_last_input)
{
	input[*length] = '\0';
	if (*length == 0 && *has_last_input) {
		memcpy(input, last_input, strlen(last_input) + 1);
		*length = strlen(input);
	} else if (*length > 0) {
		memcpy(last_input, input, *length + 1);
		*has_last_input = true;
	}
	if (command_is(input, "exit") || command_is(input, "quit"))
		return true;
	if (command_is(input, "help")) {
		print("help  show commands\ninfo  show process information\ncpu   show current CPU\ncalc  evaluate an expression\nexit  return to the shell\n");
	} else if (command_is(input, "info")) {
		print("JanOS MVP shell: one foreground process, PS/2 input, console output\n");
		print("Use calc <expression> or enter an expression directly.\n");
	} else if (command_is(input, "cpu")) {
		print("calc cpu=");
		print_integer(janos_cpu_get());
		print("\n");
	} else {
		const char *expression = input;
		if (command_is(input, "calc")) {
			expression = input + 4;
			while (*expression == ' ' || *expression == '\t')
				++expression;
		}
		(void)evaluate(expression);
	}
	print("calc> ");
	*length = 0;
	input[0] = '\0';
	return false;
}

static int interactive_ipc(uint32_t endpoint)
{
	char input[CALC_MAX_INPUT + 1];
	char last_input[CALC_MAX_INPUT + 1];
	size_t length = 0;
	bool has_last_input = false;
	input[0] = '\0';
	print("calc> ");
	for (;;) {
		struct janos_ipc_message message;
		int32_t result = janos_ipc_receive(endpoint, &message,
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
		if (!janos_input_event_pressed(event))
			continue;
		uint32_t key = janos_input_event_key(event);
		uint32_t character = janos_input_event_character(event);
		if ((event & JANOS_INPUT_EVENT_CTRL) != 0 &&
			key == JANOS_INPUT_KEY_CHARACTER &&
			(character == 'c' || character == 'C')) {
			print("^C\n");
			return 130;
		}
		if (key == JANOS_INPUT_KEY_CHARACTER && character >= 0x20u &&
			character < 0x7fu) {
			if (length < CALC_MAX_INPUT) {
				input[length++] = (char)character;
				input[length] = '\0';
				(void)write(1, &character, 1);
			}
			continue;
		}
		if (key == JANOS_INPUT_KEY_BACKSPACE) {
			if (length != 0) {
				--length;
				input[length] = '\0';
				print("\b \b");
			}
			continue;
		}
		if (key == JANOS_INPUT_KEY_ENTER) {
			print("\n");
			if (handle_line(input, &length, last_input, &has_last_input))
				return 0;
		}
	}
}

int main(int argc, char **argv)
{
	if (argc > 2) {
		uint32_t endpoint = parse_uint(argv[2]);
		if (endpoint == 0)
			return 1;
		if (argv[1][0] != '\0')
			return evaluate(argv[1]);
		return interactive_ipc(endpoint);
	}
	if (argc > 1)
		return evaluate(argv[1]);
	char input[CALC_MAX_INPUT + 1];
	char last_input[CALC_MAX_INPUT + 1];
	bool has_last_input = false;
	print("calc> ");
	for (;;) {
		calc_int32_t bytes = (calc_int32_t)read(0, input, CALC_MAX_INPUT);
		if (bytes <= 0)
			break;
		while (bytes > 0 && (input[bytes - 1] == '\n' || input[bytes - 1] == '\r'))
			--bytes;
		input[bytes] = '\0';
		if (bytes == 0 && has_last_input) {
			memcpy(input, last_input, strlen(last_input) + 1);
			bytes = (calc_int32_t)strlen(input);
		} else if (bytes > 0) {
			memcpy(last_input, input, (size_t)bytes + 1);
			has_last_input = true;
		}
		if (command_is(input, "help")) {
			print("help  show commands\ninfo  show process information\ncpu   show current CPU\ncalc  evaluate an expression\n");
			print("calc> ");
			continue;
		}
		if (command_is(input, "info")) {
			print("JanOS MVP shell: one foreground process, PS/2 input, console output\n");
			print("Use calc <expression> or enter an expression directly.\n");
			print("calc> ");
			continue;
		}
		if (command_is(input, "cpu")) {
			print("calc cpu=");
			print_integer(janos_cpu_get());
			print("\ncalc> ");
			continue;
		}
		const char *expression = input;
		if (command_is(input, "calc")) {
			expression = input + 4;
			while (*expression == ' ' || *expression == '\t')
				++expression;
		}
		calc_int32_t result;
		enum calc_status status = calc_eval(expression, (calc_size_t)strlen(expression), &result);
		if (status == CALC_OK) {
			print_integer(result);
			print("\n");
		} else {
			print("error: ");
			print(calc_status_string(status));
			print("\n");
		}
		print("calc> ");
	}
	return 0;
}
